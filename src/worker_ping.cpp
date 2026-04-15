#include "worker_ping.h"

#include "process_util.h"

#include <regex>

PingWorker::~PingWorker() {
    stop();
}

void PingWorker::start(const std::string& target, bool continuous, int timeoutSec) {
    stop();
    stopFlag_.store(false);
    out_.clear();
    thread_ = std::thread(&PingWorker::run, this, target, continuous, timeoutSec);
}

void PingWorker::stop() {
    stopFlag_.store(true);
    if (thread_.joinable()) {
        thread_.join();
    }
}

bool PingWorker::running() const {
    return thread_.joinable() && !stopFlag_.load();
}

TSQueue<PingWorkerOut>& PingWorker::output() {
    return out_;
}

void PingWorker::run(std::string target, bool continuous, int timeoutSec) {
    const std::string cmd = "ping " + target + (continuous ? " -t" : "") + " -w " + std::to_string(timeoutSec * 1000);
    ProcessHandle proc;
    if (!proc.start(cmd)) {
        out_.push({PingWorkerOut::Type::Error, "Failed to start ping", {}});
        return;
    }

    std::regex okPattern(R"(time[=<]\s*(\d+)\s*ms)");
    std::regex timeoutPattern(R"(timed out|unreachable)", std::regex_constants::icase);

    std::string line;
    while (!stopFlag_.load()) {
        if (!proc.readLineStdout(line)) {
            if (!proc.isRunning()) break;
            Sleep(20);
            continue;
        }

        out_.push({PingWorkerOut::Type::Line, line, {}});

        std::smatch m;
        if (std::regex_search(line, m, okPattern)) {
            PingWorkerOut sample;
            sample.type = PingWorkerOut::Type::Sample;
            sample.sample.rtt_ms = std::stod(m[1].str());
            out_.push(sample);
        } else if (std::regex_search(line, timeoutPattern)) {
            PingWorkerOut sample;
            sample.type = PingWorkerOut::Type::Sample;
            sample.sample.timed_out = true;
            out_.push(sample);
        }
    }

    proc.terminate();
    out_.push({PingWorkerOut::Type::Done, {}, {}});
}
