# Changelog

All notable changes to this project will be documented in this file.

## [0.1.0] - 2026-04-15

### Added
- Initial release
- iperf2 / iperf3 throughput testing (client and server modes)
- TCP and UDP support with configurable bandwidth, port, duration, parallel streams
- Ping latency monitoring with live chart
- tshark/dumpcap packet capture with BPF filter support
- Live ImPlot charts for throughput and latency
- Dark theme Win32 + ImGui/DirectX 11 rendering
- DPI-aware scaling for all display resolutions
- Bundled iperf2, iperf3, tshark (portable, no installation required)
- Npcap bundled in installer for out-of-the-box packet capture
