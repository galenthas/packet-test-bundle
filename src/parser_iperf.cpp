#include "parser_iperf.h"

#include <algorithm>
#include <cctype>

IperfParser::IperfParser() {
    tcpPattern_ = std::regex(R"(\[\s*\d+\]\s+([\d.]+)-([\d.]+)\s+sec\s+([\d.]+)\s+(\w+Bytes)\s+([\d.]+)\s+(\w+bits/sec))");
    udpPattern_ = std::regex(R"(\[\s*\d+\]\s+([\d.]+)-([\d.]+)\s+sec\s+([\d.]+)\s+(\w+Bytes)\s+([\d.]+)\s+(\w+bits/sec)\s+([\d.]+)\s+ms\s+(\d+)/\s*(\d+)\s+\(([\d.]+)%\))");
}

void IperfParser::reset() {
    nonZeroIntervalsSeen_ = false;
}

double IperfParser::toMbps(double value, const std::string& unit) {
    std::string u = unit;
    std::transform(u.begin(), u.end(), u.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (u.find("gbits") != std::string::npos) return value * 1000.0;
    if (u.find("mbits") != std::string::npos) return value;
    if (u.find("kbits") != std::string::npos) return value / 1000.0;
    return value / 1e6;
}

bool IperfParser::parseLine(const std::string& line, IperfInterval& out) {
    out = IperfInterval{};
    std::smatch m;
    if (std::regex_search(line, m, udpPattern_)) {
        out.start_sec = std::stod(m[1].str());
        out.end_sec = std::stod(m[2].str());
        out.throughput_mbps = toMbps(std::stod(m[5].str()), m[6].str());
        out.is_udp = true;
        out.jitter_ms = std::stod(m[7].str());
        out.packets_lost = std::stoi(m[8].str());
        out.packets_total = std::stoi(m[9].str());
        out.loss_pct = std::stod(m[10].str());
        out.is_sender_summary = line.find("sender") != std::string::npos;
        out.is_receiver_summary = line.find("receiver") != std::string::npos;
        out.is_summary = out.is_sender_summary || out.is_receiver_summary || (out.start_sec == 0.0 && nonZeroIntervalsSeen_);
        if (!out.is_summary && out.packets_total > 0) {
            nonZeroIntervalsSeen_ = true;
        }
        return true;
    }

    if (std::regex_search(line, m, tcpPattern_)) {
        out.start_sec = std::stod(m[1].str());
        out.end_sec = std::stod(m[2].str());
        out.throughput_mbps = toMbps(std::stod(m[5].str()), m[6].str());
        out.is_sender_summary = line.find("sender") != std::string::npos;
        out.is_receiver_summary = line.find("receiver") != std::string::npos;
        out.is_summary = out.is_sender_summary || out.is_receiver_summary || (out.start_sec == 0.0 && nonZeroIntervalsSeen_);
        if (!out.is_summary) {
            nonZeroIntervalsSeen_ = true;
        }
        return true;
    }
    return false;
}
