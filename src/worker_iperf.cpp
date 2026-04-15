#include "worker_iperf.h"

#include "process_util.h"

#include <windows.h>

namespace {
std::string makeTempFile() {
    char tmpPath[MAX_PATH];
    char tmpFile[MAX_PATH];
    GetTempPathA(MAX_PATH, tmpPath);
    GetTempFileNameA(tmpPath, "ip3", 0, tmpFile);
    return std::string(tmpFile);
}

void pushLine(TSQueue<IperfWorkerOut>& q, const std::string& text, bool isErr = false) {
    IperfWorkerOut out;
    out.type = isErr ? IperfWorkerOut::Type::Error : IperfWorkerOut::Type::Line;
    out.line = text;
    q.push(out);
}
}

IperfWorker::~IperfWorker() {
    stop();
}

void IperfWorker::start(const std::string& cmdline, bool useIperf3) {
    stop();
    stopFlag_.store(false);
    out_.clear();
    thread_ = std::thread(&IperfWorker::run, this, cmdline, useIperf3);
}

void IperfWorker::stop() {
    stopFlag_.store(true);
    if (thread_.joinable()) {
        thread_.join();
    }
}

bool IperfWorker::running() const {
    return thread_.joinable() && !stopFlag_.load();
}

TSQueue<IperfWorkerOut>& IperfWorker::output() {
    return out_;
}

void IperfWorker::run(std::string cmdline, bool useIperf3) {
    IperfParser parser;
    IperfInterval lastSummary{};
    bool hasSummary = false;

    auto processLine = [&](const std::string& line) {
        if (stopFlag_.load()) return;
        pushLine(out_, line);

        const bool isNewConn = line.find("Accepted connection") != std::string::npos
            || line.find("Connected to") != std::string::npos
            || line.find("connected with") != std::string::npos;
        if (isNewConn) {
            IperfWorkerOut msg;
            msg.type = IperfWorkerOut::Type::NewConnection;
            msg.line = line;
            out_.push(msg);
            parser.reset();
            hasSummary = false;
            return;
        }

        IperfInterval iv;
        if (!parser.parseLine(line, iv)) {
            return;
        }

        IperfWorkerOut out;
        out.interval = iv;
        if (iv.is_summary) {
            out.type = IperfWorkerOut::Type::FinalResult;
            lastSummary = iv;
            hasSummary = true;
        } else {
            out.type = IperfWorkerOut::Type::Interval;
        }
        this->out_.push(out);
    };

    if (useIperf3) {
        const std::string logFile = makeTempFile();
        const std::string fullCmd = cmdline + " --logfile \"" + logFile + "\"";
        ProcessHandle proc;
        if (!proc.start(fullCmd, false)) {
            pushLine(out_, "[Error] Failed to start iperf3 process", true);
            out_.push({IperfWorkerOut::Type::Done, {}, {}});
            return;
        }

        HANDLE hLog = INVALID_HANDLE_VALUE;
        for (int i = 0; i < 50 && !stopFlag_.load(); ++i) {
            hLog = CreateFileA(logFile.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (hLog != INVALID_HANDLE_VALUE) break;
            Sleep(100);
        }
        if (hLog == INVALID_HANDLE_VALUE) {
            pushLine(out_, "[Error] Could not open iperf3 log file", true);
            proc.terminate();
            DeleteFileA(logFile.c_str());
            out_.push({IperfWorkerOut::Type::Done, {}, {}});
            return;
        }

        std::string lineBuf;
        int idleReadsAfterExit = 0;
        while (!stopFlag_.load()) {
            char chunk[4096];
            DWORD bytesRead = 0;
            const BOOL ok = ReadFile(hLog, chunk, sizeof(chunk), &bytesRead, nullptr);
            if (ok && bytesRead > 0) {
                idleReadsAfterExit = 0;
                for (DWORD i = 0; i < bytesRead; ++i) {
                    if (chunk[i] == '\n') {
                        if (!lineBuf.empty() && lineBuf.back() == '\r') lineBuf.pop_back();
                        if (!lineBuf.empty()) processLine(lineBuf);
                        lineBuf.clear();
                    } else {
                        lineBuf.push_back(chunk[i]);
                    }
                }
            } else {
                if (!proc.isRunning()) {
                    if (++idleReadsAfterExit >= 10) break;
                }
                Sleep(50);
            }
        }
        if (!lineBuf.empty()) processLine(lineBuf);
        CloseHandle(hLog);
        proc.terminate();
        DeleteFileA(logFile.c_str());
    } else {
        ProcessHandle proc;
        if (!proc.start(cmdline)) {
            pushLine(out_, "[Error] Failed to start iperf2 process", true);
            out_.push({IperfWorkerOut::Type::Done, {}, {}});
            return;
        }
        std::string line;
        while (!stopFlag_.load()) {
            if (proc.readLineStdout(line)) {
                processLine(line);
            } else {
                if (!proc.isRunning()) break;
                Sleep(10);
            }
        }
        proc.terminate();
    }

    IperfWorkerOut done;
    done.type = IperfWorkerOut::Type::Done;
    if (hasSummary) {
        done.interval = lastSummary;
    }
    out_.push(done);
}
