#pragma once

#include "ts_queue.h"

#include <atomic>
#include <string>
#include <thread>
#include <vector>

struct InterfaceInfo {
    std::string index;
    std::string description;
    std::string raw_description;
};

struct PcapWorkerOut {
    enum class Type {
        Ready,
        PacketCount,
        Error,
        Done
    } type = Type::Done;

    int packet_count = 0;
    std::string line;
};

std::string findTshark();
std::vector<InterfaceInfo> listTsharkInterfaces(const std::string& tsharkPath);

class PcapWorker {
public:
    PcapWorker() = default;
    ~PcapWorker();

    void start(const std::string& tsharkPath, const std::string& ifaceIndex, const std::string& filter, int expectedDurationSec = 0);
    void requestStop();
    void stop();
    bool running() const;
    TSQueue<PcapWorkerOut>& output();

private:
    std::thread thread_;
    std::atomic<bool> stopFlag_{false};
    TSQueue<PcapWorkerOut> out_;

    void run(std::string tsharkPath, std::string ifaceIndex, std::string filter, int expectedDurationSec);
    int countCapturedPackets(const std::string& tsharkPath, const std::string& captureFile);
};
