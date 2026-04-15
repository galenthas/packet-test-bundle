#pragma once

#include "worker_iperf.h"
#include "worker_pcap.h"
#include "worker_ping.h"

#include <deque>
#include <string>
#include <vector>
#include <winsock2.h>
#include <windows.h>

struct AppConfig {
    bool useIperf3 = true;
    bool serverMode = false;
    bool udp = true;
    int port = 5201;
    std::string targetIp;
    std::string bindAddr;
    int duration = 10;
    int interval = 1;
    int parallel = 1;
    std::string bandwidthMbps;
    std::string packetLength;
    bool bidirectional = false;
    bool multicast = false;
    bool broadcast = false;
    std::string multicastGroup;
    bool pcapEnabled = false;
    int pcapInterfaceIndex = 0;
    std::string pcapFilter = "udp port 5201";
    std::string pingTarget;
};

struct SummaryState {
    double throughputMbps = 0.0;
    double jitterMs = 0.0;
    double lossPct = 0.0;
    int packetsTotal = 0;
    int packetsLost = 0;
    double pingMinMs = 0.0;
    double pingAvgMs = 0.0;
    double pingMaxMs = 0.0;
    int pingCount = 0;
    int pcapCaptured = -1;
    int pcapIperfPackets = -1;
    int pcapSenderPackets = -1;
    int pcapReceiverPackets = -1;
};

class App {
public:
    App();
    ~App();
    int run();

private:
    AppConfig cfg_;
    SummaryState summary_;
    IperfWorker iperfWorker_;
    PingWorker pingWorker_;
    PcapWorker pcapWorker_;
    std::vector<InterfaceInfo> pcapInterfaces_;
    std::string tsharkPath_;
    std::string pendingIperfCmd_;
    bool pendingIperfUseV3_ = true;
    bool pendingIperfLaunch_ = false;
    int pingSampleCounter_ = 0;
    std::deque<std::string> iperfLog_;
    std::deque<std::string> pingLog_;
    std::vector<float> iperfPoints_;
    std::vector<double> pingXs_;
    std::vector<float> pingPoints_;
    std::vector<double> pingTimeoutXs_;
    std::string iperfStatus_ = "Stopped";
    std::string pingStatus_ = "Stopped";
    std::string pcapInterfaceError_;
    ULONGLONG pcapReadyWaitStartTick_ = 0;
    bool pcapWaitingForReady_ = false;
    ULONGLONG clientPcapStopDeadlineTick_ = 0;
    bool clientPcapStopPending_ = false;
    ULONGLONG serverPcapStopDeadlineTick_ = 0;
    bool serverPcapStopPending_ = false;
    bool serverPcapActiveTest_ = false;
    bool winsockReady_ = false;
    SOCKET discoverySock_ = INVALID_SOCKET;
    std::string discoveryBoundIp_;
    bool discoveryBoundAsServer_ = false;
    ULONGLONG discoveryLastRequestTick_ = 0;
    ULONGLONG discoveryLastAnnounceTick_ = 0;
    std::string lastDiscoveredIp_;
    mutable int resolvedInterfaceCacheIndex_ = -2;
    mutable size_t resolvedInterfaceCacheSize_ = 0;
    mutable std::string resolvedInterfaceCacheIp_;

    bool createWindow();
    void destroyWindow();
    bool createDeviceD3D(void* hwnd);
    void cleanupDeviceD3D();
    void createRenderTarget();
    void cleanupRenderTarget();
    void renderUi();
    void drawConfigPanel();
    void drawSummaryPanel();
    void drawLogAndCharts();
    void drainWorkerOutputs();
    void startIperf();
    void stopIperf();
    void startPing();
    void stopPing();
    void startPcapCapture(int expectedDurationSec, bool launchIperfAfterReady, bool resetUi = true);
    void launchIperfNow();
    void resetIperfUi();
    void resetPingUi();
    void resetPcapUi();
    void appendLog(std::deque<std::string>& log, const std::string& line);
    void refreshPcapInterfaces();
    void syncPortAndFilter();
    void applyTheme();
    void initDiscovery();
    void shutdownDiscovery();
    void refreshDiscoverySocket();
    void tickDiscovery();
    std::string resolveSelectedInterfaceIpv4() const;
    std::string buildIperfCommand() const;
    std::string findIperfBinary() const;
};
