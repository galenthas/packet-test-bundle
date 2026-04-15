#pragma once

#include <regex>
#include <string>

struct IperfInterval {
    double start_sec = 0.0;
    double end_sec = 0.0;
    double throughput_mbps = 0.0;
    bool is_udp = false;
    double jitter_ms = 0.0;
    int packets_lost = 0;
    int packets_total = 0;
    double loss_pct = 0.0;
    bool is_summary = false;
    bool is_sender_summary = false;
    bool is_receiver_summary = false;
};

class IperfParser {
public:
    IperfParser();
    bool parseLine(const std::string& line, IperfInterval& out);
    void reset();

private:
    std::regex tcpPattern_;
    std::regex udpPattern_;
    bool nonZeroIntervalsSeen_ = false;

    double toMbps(double value, const std::string& unit);
};
