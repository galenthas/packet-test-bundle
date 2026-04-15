#include "worker_pcap.h"

#include "process_util.h"

#include <filesystem>
#include <regex>
#include <set>
#include <sstream>
#include <windows.h>

namespace fs = std::filesystem;

namespace {
std::string executableDir() {
    char buffer[MAX_PATH];
    const DWORD len = GetModuleFileNameA(nullptr, buffer, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        return ".";
    }
    return fs::path(buffer).parent_path().string();
}

bool fileExists(const fs::path& path) {
    std::error_code ec;
    return fs::is_regular_file(path, ec);
}

std::string trimCopy(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

std::string prettifyInterfaceName(const std::string& raw) {
    std::string desc = trimCopy(raw);
    std::smatch match;
    static const std::regex parenPattern(R"(^[^(]+\(([^()]*)\)\s*$)");
    if (std::regex_match(desc, match, parenPattern)) {
        desc = trimCopy(match[1].str());
    }
    const auto bracketPos = desc.find(" [");
    if (bracketPos != std::string::npos) {
        desc = trimCopy(desc.substr(0, bracketPos));
    }
    static const std::regex npfPattern(R"(\\Device\\NPF_\{[A-Fa-f0-9\-]+\})");
    desc = std::regex_replace(desc, npfPattern, "");
    desc = trimCopy(desc);
    return desc.empty() ? trimCopy(raw) : desc;
}

bool isPseudoInterface(const InterfaceInfo& iface) {
    std::string text = iface.description + " " + iface.raw_description;
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text.find("event tracing for windows") != std::string::npos
        || text.find("(etw)") != std::string::npos
        || text.find("etw reader") != std::string::npos
        || text.find("etwdump") != std::string::npos
        || text.find("usbpcap") != std::string::npos
        || text.find("sshdump") != std::string::npos
        || text.find("ciscodump") != std::string::npos
        || text.find("randpkt") != std::string::npos
        || text.find("udpdump") != std::string::npos
        || text.find("wifidump") != std::string::npos;
}

std::vector<InterfaceInfo> collectInterfacesFromTool(const std::string& toolPath) {
    std::vector<InterfaceInfo> interfaces;
    if (toolPath.empty()) return interfaces;

    ProcessHandle proc;
    if (!proc.start("\"" + toolPath + "\" -D")) {
        return interfaces;
    }

    std::regex pattern(R"((\d+)\.\s+(.+))");
    std::set<std::string> seen;
    auto consumeLine = [&](const std::string& line) {
        std::smatch match;
        if (!std::regex_match(line, match, pattern)) return;
        const std::string index = trimCopy(match[1].str());
        const std::string raw = trimCopy(match[2].str());
        if (!index.empty() && seen.insert(index).second) {
            interfaces.push_back({index, prettifyInterfaceName(raw), raw});
        }
    };

    std::string line;
    int idleSpins = 0;
    while (idleSpins < 200) {
        bool sawOutput = false;
        while (proc.readLineStdout(line)) {
            sawOutput = true;
            consumeLine(line);
        }
        while (proc.readLineStderr(line)) {
            sawOutput = true;
            consumeLine(line);
        }
        if (!proc.isRunning()) {
            while (proc.readLineStdout(line)) consumeLine(line);
            while (proc.readLineStderr(line)) consumeLine(line);
            break;
        }
        if (sawOutput) {
            idleSpins = 0;
        } else {
            ++idleSpins;
            Sleep(25);
        }
    }
    proc.terminate();
    return interfaces;
}

std::string findDumpcapForTshark(const std::string& tsharkPath) {
    if (tsharkPath.empty()) return {};
    const fs::path sibling = fs::path(tsharkPath).parent_path() / "dumpcap.exe";
    if (fileExists(sibling)) return sibling.string();
    return {};
}

std::string makeTempCaptureFile() {
    char tempPath[MAX_PATH];
    char tempFile[MAX_PATH];
    GetTempPathA(MAX_PATH, tempPath);
    GetTempFileNameA(tempPath, "pcp", 0, tempFile);
    std::string path = tempFile;
    path += ".pcap";
    MoveFileExA(tempFile, path.c_str(), MOVEFILE_REPLACE_EXISTING);
    return path;
}
}

std::string findTshark() {
    const fs::path bundled = fs::path(executableDir()) / "bin" / "tshark" / "tshark.exe";
    if (fileExists(bundled)) return bundled.string();
    char found[MAX_PATH];
    const DWORD rc = SearchPathA(nullptr, "tshark.exe", nullptr, MAX_PATH, found, nullptr);
    if (rc > 0 && rc < MAX_PATH) return std::string(found);
    return {};
}

std::vector<InterfaceInfo> listTsharkInterfaces(const std::string& tsharkPath) {
    const std::string dumpcapPath = findDumpcapForTshark(tsharkPath);
    auto interfaces = collectInterfacesFromTool(dumpcapPath);
    if (interfaces.empty()) interfaces = collectInterfacesFromTool(tsharkPath);
    std::vector<InterfaceInfo> filtered;
    for (const auto& iface : interfaces) {
        if (!isPseudoInterface(iface)) filtered.push_back(iface);
    }
    return filtered.empty() ? std::vector<InterfaceInfo>{} : filtered;
}

PcapWorker::~PcapWorker() {
    stop();
}

void PcapWorker::start(const std::string& tsharkPath, const std::string& ifaceIndex, const std::string& filter, int expectedDurationSec) {
    stop();
    stopFlag_.store(false);
    out_.clear();
    thread_ = std::thread(&PcapWorker::run, this, tsharkPath, ifaceIndex, filter, expectedDurationSec);
}

void PcapWorker::stop() {
    stopFlag_.store(true);
    if (thread_.joinable()) thread_.join();
}

void PcapWorker::requestStop() {
    stopFlag_.store(true);
}

bool PcapWorker::running() const {
    return thread_.joinable() && !stopFlag_.load();
}

TSQueue<PcapWorkerOut>& PcapWorker::output() {
    return out_;
}

int PcapWorker::countCapturedPackets(const std::string& tsharkPath, const std::string& captureFile) {
    ProcessHandle proc;
    if (!proc.start("\"" + tsharkPath + "\" -r \"" + captureFile + "\" -T fields -e frame.number")) {
        return -1;
    }
    int count = 0;
    std::string line;
    int idleSpins = 0;
    while (idleSpins < 40) {
        bool sawOutput = false;
        while (proc.readLineStdout(line)) {
            sawOutput = true;
            if (!trimCopy(line).empty()) ++count;
        }
        while (proc.readLineStderr(line)) {
            sawOutput = true;
        }
        if (!proc.isRunning()) {
            while (proc.readLineStdout(line)) {
                if (!trimCopy(line).empty()) ++count;
            }
            while (proc.readLineStderr(line)) {}
            break;
        }
        if (sawOutput) {
            idleSpins = 0;
        } else {
            ++idleSpins;
            Sleep(10);
        }
    }
    proc.terminate();
    return count;
}

void PcapWorker::run(std::string tsharkPath, std::string ifaceIndex, std::string filter, int expectedDurationSec) {
    if (tsharkPath.empty()) {
        out_.push({PcapWorkerOut::Type::Error, 0, "tshark not found"});
        out_.push({PcapWorkerOut::Type::Done, 0, {}});
        return;
    }
    const std::string dumpcapPath = findDumpcapForTshark(tsharkPath);
    if (dumpcapPath.empty()) {
        out_.push({PcapWorkerOut::Type::Error, 0, "dumpcap not found"});
        out_.push({PcapWorkerOut::Type::Done, 0, {}});
        return;
    }

    const std::string captureFile = makeTempCaptureFile();
    std::ostringstream cmd;
    cmd << '"' << dumpcapPath << "\" -i " << ifaceIndex;
    if (!filter.empty()) cmd << " -f \"" << filter << '"';
    if (expectedDurationSec > 0) cmd << " -a duration:" << (expectedDurationSec + 2);
    cmd << " -F pcap -Q -q -w \"" << captureFile << '"';

    ProcessHandle proc;
    if (!proc.start(cmd.str())) {
        out_.push({PcapWorkerOut::Type::Error, 0, "Failed to start dumpcap"});
        out_.push({PcapWorkerOut::Type::Done, 0, {}});
        return;
    }

    out_.push({PcapWorkerOut::Type::Ready, 0, {}});
    int lastCount = -1;
    int pollTickMs = 0;
    while (!stopFlag_.load()) {
        Sleep(100);
        pollTickMs += 100;
        if (pollTickMs < 1000) continue;
        pollTickMs = 0;
        const int count = countCapturedPackets(tsharkPath, captureFile);
        if (count >= 0 && count != lastCount) {
            lastCount = count;
            out_.push({PcapWorkerOut::Type::PacketCount, count, {}});
        }
        if (!proc.isRunning() && expectedDurationSec > 0) {
            break;
        }
    }

    proc.terminate();
    int bestFinalCount = -1;
    for (int attempt = 0; attempt < 3; ++attempt) {
        Sleep(100);
        const int finalCount = countCapturedPackets(tsharkPath, captureFile);
        if (finalCount > bestFinalCount) {
            bestFinalCount = finalCount;
        }
    }
    if (bestFinalCount >= 0) {
        out_.push({PcapWorkerOut::Type::PacketCount, bestFinalCount, {}});
    }
    std::error_code ec;
    fs::remove(captureFile, ec);
    out_.push({PcapWorkerOut::Type::Done, 0, {}});
}
