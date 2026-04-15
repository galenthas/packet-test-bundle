#pragma once

#include <string>

class IperfCommandBuilder {
public:
    IperfCommandBuilder(std::string binaryPath, bool useIperf3);

    void serverMode();
    void clientMode(const std::string& targetIp);
    void udp();
    void port(int port);
    void duration(int seconds);
    void interval(int seconds);
    void parallel(int count);
    void bandwidth(const std::string& value);
    void packetLength(const std::string& value);
    void bindAddress(const std::string& value);
    void bidirectional();

    std::string build() const;

private:
    std::string binaryPath_;
    bool useIperf3_ = true;
    std::string modeArgs_;
    std::string extraArgs_;
};
