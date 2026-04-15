#pragma once

#include "ts_queue.h"

#include <atomic>
#include <string>
#include <thread>

struct PingSample {
    double rtt_ms = 0.0;
    bool timed_out = false;
};

struct PingWorkerOut {
    enum class Type {
        Line,
        Sample,
        Error,
        Done
    } type = Type::Line;

    std::string line;
    PingSample sample;
};

class PingWorker {
public:
    PingWorker() = default;
    ~PingWorker();

    void start(const std::string& target, bool continuous, int timeoutSec);
    void stop();
    bool running() const;
    TSQueue<PingWorkerOut>& output();

private:
    std::thread thread_;
    std::atomic<bool> stopFlag_{false};
    TSQueue<PingWorkerOut> out_;

    void run(std::string target, bool continuous, int timeoutSec);
};
