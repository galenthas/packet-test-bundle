#include <cassert>
#include <cstdio>
#include <string>

#include "command_builder.h"
#include "parser_iperf.h"

static int g_passed = 0;
static int g_failed = 0;

#define CHECK(expr) do { \
    if (expr) { \
        ++g_passed; \
    } else { \
        ++g_failed; \
        printf("FAIL: %s  (line %d)\n", #expr, __LINE__); \
    } \
} while(0)

// ── IperfCommandBuilder ───────────────────────────────────────────────────────

void test_command_builder_empty() {
    IperfCommandBuilder b("iperf3.exe", true);
    CHECK(b.build().empty()); // no mode set
}

void test_command_builder_server() {
    IperfCommandBuilder b("C:/bin/iperf3.exe", true);
    b.serverMode();
    CHECK(b.build() == "\"C:/bin/iperf3.exe\" -s");
}

void test_command_builder_client_basic() {
    IperfCommandBuilder b("iperf3.exe", true);
    b.clientMode("192.168.1.1");
    CHECK(b.build() == "\"iperf3.exe\" -c 192.168.1.1");
}

void test_command_builder_client_full() {
    IperfCommandBuilder b("iperf3.exe", true);
    b.clientMode("10.0.0.1");
    b.port(5202);
    b.duration(10);
    b.interval(1);
    b.parallel(2);
    b.bandwidth("100M");
    const std::string cmd = b.build();
    CHECK(cmd.find("-c 10.0.0.1") != std::string::npos);
    CHECK(cmd.find("-p 5202") != std::string::npos);
    CHECK(cmd.find("-t 10") != std::string::npos);
    CHECK(cmd.find("-i 1") != std::string::npos);
    CHECK(cmd.find("-P 2") != std::string::npos);
    CHECK(cmd.find("-b 100M") != std::string::npos);
}

void test_command_builder_udp() {
    IperfCommandBuilder b("iperf3.exe", true);
    b.clientMode("10.0.0.1");
    b.udp();
    CHECK(b.build().find("-u") != std::string::npos);
}

void test_command_builder_bidirectional_iperf3() {
    IperfCommandBuilder b("iperf3.exe", true);
    b.clientMode("10.0.0.1");
    b.bidirectional();
    CHECK(b.build().find("--bidir") != std::string::npos);
}

void test_command_builder_bidirectional_iperf2() {
    IperfCommandBuilder b("iperf.exe", false);
    b.clientMode("10.0.0.1");
    b.bidirectional();
    CHECK(b.build().find("-d") != std::string::npos);
}

void test_command_builder_zero_duration_ignored() {
    IperfCommandBuilder b("iperf3.exe", true);
    b.clientMode("10.0.0.1");
    b.duration(0);
    CHECK(b.build().find("-t") == std::string::npos);
}

void test_command_builder_bind_address() {
    IperfCommandBuilder b("iperf3.exe", true);
    b.clientMode("10.0.0.1");
    b.bindAddress("192.168.1.5");
    CHECK(b.build().find("-B 192.168.1.5") != std::string::npos);
}

// ── IperfParser ───────────────────────────────────────────────────────────────

void test_parser_tcp_interval() {
    IperfParser p;
    IperfInterval out;
    const std::string line = "[  5]   0.00-1.00   sec   112 MBytes   940 Mbits/sec";
    CHECK(p.parseLine(line, out));
    CHECK(out.start_sec == 0.0);
    CHECK(out.end_sec == 1.0);
    CHECK(out.throughput_mbps > 900.0 && out.throughput_mbps < 1000.0);
    CHECK(!out.is_udp);
    CHECK(!out.is_summary);
}

void test_parser_tcp_gbps() {
    IperfParser p;
    IperfInterval out;
    const std::string line = "[  5]   0.00-1.00   sec  1.09 GBytes  9.37 Gbits/sec";
    CHECK(p.parseLine(line, out));
    CHECK(out.throughput_mbps > 9000.0);
}

void test_parser_tcp_kbps() {
    IperfParser p;
    IperfInterval out;
    const std::string line = "[  5]   0.00-1.00   sec   112 KBytes   900 Kbits/sec";
    CHECK(p.parseLine(line, out));
    CHECK(out.throughput_mbps < 1.0);
}

void test_parser_udp_interval() {
    IperfParser p;
    IperfInterval out;
    const std::string line = "[  5]   1.00-2.00   sec   128 KBytes  1.05 Mbits/sec  0.123 ms  0/ 90 (0%)";
    CHECK(p.parseLine(line, out));
    CHECK(out.is_udp);
    CHECK(out.jitter_ms > 0.1);
    CHECK(out.packets_total == 90);
    CHECK(out.packets_lost == 0);
    CHECK(out.loss_pct == 0.0);
}

void test_parser_udp_loss() {
    IperfParser p;
    IperfInterval out;
    const std::string line = "[  5]   0.00-10.00  sec  1.25 MBytes  1.05 Mbits/sec  2.345 ms  5/ 100 (5%)";
    CHECK(p.parseLine(line, out));
    CHECK(out.is_udp);
    CHECK(out.packets_lost == 5);
    CHECK(out.packets_total == 100);
    CHECK(out.loss_pct == 5.0);
}

void test_parser_tcp_sender_summary() {
    IperfParser p;
    IperfInterval out;
    // First interval to set nonZeroIntervalsSeen_
    p.parseLine("[  5]   0.00-1.00   sec   112 MBytes   940 Mbits/sec", out);
    const std::string line = "[  5]   0.00-10.00  sec  1.09 GBytes   940 Mbits/sec                  sender";
    CHECK(p.parseLine(line, out));
    CHECK(out.is_summary);
    CHECK(out.is_sender_summary);
}

void test_parser_no_match() {
    IperfParser p;
    IperfInterval out;
    CHECK(!p.parseLine("Connecting to host 10.0.0.1, port 5201", out));
    CHECK(!p.parseLine("", out));
    CHECK(!p.parseLine("[ ID] Interval           Transfer     Bitrate", out));
}

void test_parser_reset() {
    IperfParser p;
    IperfInterval out;
    p.parseLine("[  5]   0.00-1.00   sec   112 MBytes   940 Mbits/sec", out);
    p.reset();
    // After reset, a 0.0-N.0 interval should not be treated as summary
    const std::string line = "[  5]   0.00-10.00  sec  1.09 GBytes   940 Mbits/sec";
    CHECK(p.parseLine(line, out));
    CHECK(!out.is_summary);
}

int main() {
    printf("Running Packet Test Bundle unit tests...\n\n");

    test_command_builder_empty();
    test_command_builder_server();
    test_command_builder_client_basic();
    test_command_builder_client_full();
    test_command_builder_udp();
    test_command_builder_bidirectional_iperf3();
    test_command_builder_bidirectional_iperf2();
    test_command_builder_zero_duration_ignored();
    test_command_builder_bind_address();

    test_parser_tcp_interval();
    test_parser_tcp_gbps();
    test_parser_tcp_kbps();
    test_parser_udp_interval();
    test_parser_udp_loss();
    test_parser_tcp_sender_summary();
    test_parser_no_match();
    test_parser_reset();

    printf("\nResults: %d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
