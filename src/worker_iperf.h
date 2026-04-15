#pragma once

#include "parser_iperf.h"
#include "ts_queue.h"

#include <atomic>
#include <string>
#include <thread>

struct IperfWorkerOut {
    enum class Type {
        Line,
        Interval,
        FinalResult,
        Error,
        NewConnection,
        Done
    } type = Type::Line;

    std::string line;
    IperfInterval interval;
};

class IperfWorker {
public:
    IperfWorker() = default;
    ~IperfWorker();

    void start(const std::string& cmdline, bool useIperf3);
    void stop();
    bool running() const;
    TSQueue<IperfWorkerOut>& output();

private:
    std::thread thread_;
    std::atomic<bool> stopFlag_{false};
    TSQueue<IperfWorkerOut> out_;

    void run(std::string cmdline, bool useIperf3);
};
