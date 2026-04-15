#pragma once

#include <string>
#include <windows.h>

class ProcessHandle {
public:
    ProcessHandle() = default;
    ~ProcessHandle();

    bool start(const std::string& commandLine, bool redirectStdIo = true);
    bool readLineStdout(std::string& line);
    bool readLineStderr(std::string& line);
    bool isRunning() const;
    void terminate();

private:
    HANDLE process_ = nullptr;
    HANDLE thread_ = nullptr;
    HANDLE stdoutRead_ = nullptr;
    HANDLE stderrRead_ = nullptr;
    HANDLE stdoutWrite_ = nullptr;
    HANDLE stderrWrite_ = nullptr;
    std::string stdoutBuffer_;
    std::string stderrBuffer_;

    bool readLineFromHandle(HANDLE handle, std::string& buffer, std::string& line);
    void closeAll();
};
