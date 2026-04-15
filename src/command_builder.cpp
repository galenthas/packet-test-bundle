#include "command_builder.h"

IperfCommandBuilder::IperfCommandBuilder(std::string binaryPath, bool useIperf3)
    : binaryPath_(std::move(binaryPath)), useIperf3_(useIperf3) {}

void IperfCommandBuilder::serverMode() { modeArgs_ = " -s"; }
void IperfCommandBuilder::clientMode(const std::string& targetIp) { modeArgs_ = " -c " + targetIp; }
void IperfCommandBuilder::udp() { extraArgs_ += " -u"; }
void IperfCommandBuilder::port(int port) { extraArgs_ += " -p " + std::to_string(port); }
void IperfCommandBuilder::duration(int seconds) { if (seconds > 0) extraArgs_ += " -t " + std::to_string(seconds); }
void IperfCommandBuilder::interval(int seconds) { if (seconds > 0) extraArgs_ += " -i " + std::to_string(seconds); }
void IperfCommandBuilder::parallel(int count) { if (count > 1) extraArgs_ += " -P " + std::to_string(count); }
void IperfCommandBuilder::bandwidth(const std::string& value) { if (!value.empty()) extraArgs_ += " -b " + value; }
void IperfCommandBuilder::packetLength(const std::string& value) { if (!value.empty()) extraArgs_ += " -l " + value; }
void IperfCommandBuilder::bindAddress(const std::string& value) { if (!value.empty()) extraArgs_ += " -B " + value; }
void IperfCommandBuilder::bidirectional() { extraArgs_ += useIperf3_ ? " --bidir" : " -d"; }

std::string IperfCommandBuilder::build() const {
    if (binaryPath_.empty() || modeArgs_.empty()) {
        return {};
    }
    return "\"" + binaryPath_ + "\"" + modeArgs_ + extraArgs_;
}
