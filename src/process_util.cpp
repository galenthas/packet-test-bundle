#include "process_util.h"

#include <vector>

namespace {
bool createPipePair(HANDLE& readHandle, HANDLE& writeHandle) {
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    if (!CreatePipe(&readHandle, &writeHandle, &sa, 0)) {
        return false;
    }
    SetHandleInformation(readHandle, HANDLE_FLAG_INHERIT, 0);
    return true;
}
}

ProcessHandle::~ProcessHandle() {
    terminate();
    closeAll();
}

bool ProcessHandle::start(const std::string& commandLine, bool redirectStdIo) {
    terminate();
    closeAll();

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    if (redirectStdIo) {
        if (!createPipePair(stdoutRead_, stdoutWrite_) || !createPipePair(stderrRead_, stderrWrite_)) {
            closeAll();
            return false;
        }
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdOutput = stdoutWrite_;
        si.hStdError = stderrWrite_;
        si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    }

    PROCESS_INFORMATION pi{};
    std::vector<char> cmd(commandLine.begin(), commandLine.end());
    cmd.push_back('\0');
    const BOOL ok = CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, redirectStdIo ? TRUE : FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    if (!ok) {
        closeAll();
        return false;
    }

    process_ = pi.hProcess;
    thread_ = pi.hThread;

    if (stdoutWrite_) {
        CloseHandle(stdoutWrite_);
        stdoutWrite_ = nullptr;
    }
    if (stderrWrite_) {
        CloseHandle(stderrWrite_);
        stderrWrite_ = nullptr;
    }
    stdoutBuffer_.clear();
    stderrBuffer_.clear();
    return true;
}

bool ProcessHandle::readLineFromHandle(HANDLE handle, std::string& buffer, std::string& line) {
    line.clear();
    if (!handle) {
        return false;
    }

    for (;;) {
        const auto newlinePos = buffer.find('\n');
        if (newlinePos != std::string::npos) {
            line = buffer.substr(0, newlinePos);
            buffer.erase(0, newlinePos + 1);
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            return true;
        }

        DWORD available = 0;
        if (!PeekNamedPipe(handle, nullptr, 0, nullptr, &available, nullptr) || available == 0) {
            return false;
        }

        char chunk[512];
        DWORD bytesRead = 0;
        if (!ReadFile(handle, chunk, sizeof(chunk), &bytesRead, nullptr) || bytesRead == 0) {
            return false;
        }
        buffer.append(chunk, chunk + bytesRead);
    }
}

bool ProcessHandle::readLineStdout(std::string& line) {
    return readLineFromHandle(stdoutRead_, stdoutBuffer_, line);
}

bool ProcessHandle::readLineStderr(std::string& line) {
    return readLineFromHandle(stderrRead_, stderrBuffer_, line);
}

bool ProcessHandle::isRunning() const {
    if (!process_) {
        return false;
    }
    DWORD exitCode = 0;
    if (!GetExitCodeProcess(process_, &exitCode)) {
        return false;
    }
    return exitCode == STILL_ACTIVE;
}

void ProcessHandle::terminate() {
    if (process_ && isRunning()) {
        TerminateProcess(process_, 1);
        WaitForSingleObject(process_, 3000);
    }
    if (thread_) {
        CloseHandle(thread_);
        thread_ = nullptr;
    }
    if (process_) {
        CloseHandle(process_);
        process_ = nullptr;
    }
}

void ProcessHandle::closeAll() {
    if (stdoutRead_) { CloseHandle(stdoutRead_); stdoutRead_ = nullptr; }
    if (stdoutWrite_) { CloseHandle(stdoutWrite_); stdoutWrite_ = nullptr; }
    if (stderrRead_) { CloseHandle(stderrRead_); stderrRead_ = nullptr; }
    if (stderrWrite_) { CloseHandle(stderrWrite_); stderrWrite_ = nullptr; }
}
