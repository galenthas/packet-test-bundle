# Packet Test Bundle — Windows GUI for iperf2, iperf3 & tshark

A free, open-source Windows GUI for network bandwidth and latency testing. Bundles **iperf2**, **iperf3**, **tshark**, and **ping** into a single portable application — no command line required. Measure TCP/UDP throughput between two machines, capture packets with BPF filters, and watch live latency charts, all from one dark-themed native interface.

![Tests](https://github.com/galenthas/packet-test-bundle/actions/workflows/test.yml/badge.svg)
![Platform](https://img.shields.io/badge/platform-Windows%2010%2F11-blue)
![Language](https://img.shields.io/badge/language-C%2B%2B17-lightgrey)
![License](https://img.shields.io/badge/license-MIT-green)
![Version](https://img.shields.io/github/v/release/galenthas/packet-test-bundle)

## Use Cases

- **LAN / WAN speed test** — measure real TCP or UDP throughput between two Windows machines
- **Network troubleshooting** — capture live packets with tshark and BPF filters without opening a terminal
- **Latency monitoring** — run a continuous ping and watch the latency graph spike in real time
- **iperf3 GUI** — a graphical front-end for iperf3 on Windows with client and server modes

## Features

- **iperf2 / iperf3** — TCP/UDP throughput testing, client and server modes, configurable duration and parallel streams
- **Ping** — continuous latency monitoring with a live scrolling chart
- **Packet capture** — tshark-based network capture with BPF filter support, no Wireshark install needed
- **Live charts** — ImPlot-based throughput and latency graphs that update in real time
- **Dark theme** — native Win32 + ImGui/DirectX 11 rendering
- **DPI-aware** — scales correctly on all display resolutions including HiDPI / 4K monitors
- **Portable** — single `.exe`, no installation required (packet capture needs Npcap)

## Installation

Download the latest release from the [Releases page](https://github.com/galenthas/packet-test-bundle/releases):

- **`PacketTestBundle_vX.X.X_Setup.exe`** — Windows installer (includes Npcap for packet capture)
- **`PacketTestBundle.exe`** — Portable executable

> **Note:** Packet capture (tshark) requires [Npcap](https://npcap.com). The installer handles this automatically.

## Build

Requires Visual Studio 2022 Build Tools and CMake.

```cmd
cmake -S packettestbundle -B packettestbundle\build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build packettestbundle\build
```

ImGui and ImPlot are fetched automatically via CMake FetchContent.

### Run tests

```cmd
cmake -S packettestbundle -B packettestbundle\build_test -DBUILD_TESTING=ON
cmake --build packettestbundle\build_test --target run_tests
```

### Build installer

Requires [Inno Setup 6](https://jrsoftware.org/isinfo.php).

```cmd
ISCC /DMyAppVersion=0.1.0 packettestbundle\installer.iss
```

## Project Structure

| File | Description |
|---|---|
| `src/app.cpp/.h` | Main application, ImGui/DX11 render loop |
| `src/app_meta.h` | App name and version string |
| `src/version.h` | Single-source version definition |
| `src/worker_iperf.cpp/.h` | iperf2/iperf3 worker thread |
| `src/worker_ping.cpp/.h` | Ping worker thread |
| `src/worker_pcap.cpp/.h` | tshark/dumpcap capture worker |
| `src/command_builder.cpp/.h` | iperf command line builder |
| `src/parser_iperf.cpp/.h` | iperf output parser |
| `src/process_util.cpp/.h` | Child process launch and I/O |
| `installer.iss` | Inno Setup installer script |

## Versioning

Version is defined in `src/version.h`. To release a new version, update the defines and tag:

```c
#define VERSION_MAJOR 0
#define VERSION_MINOR 1
#define VERSION_PATCH 1
```

```bash
git tag v0.1.1 && git push origin v0.1.1
```

CI will automatically build the installer and portable `.exe` and attach both to the GitHub Release.

## Dependencies

| Tool | Bundled | License |
|---|---|---|
| iperf2 | Yes | BSD |
| iperf3 | Yes | BSD |
| tshark | Yes | GPLv2 |
| Npcap | Installer only | Npcap License |
| Dear ImGui | FetchContent | MIT |
| ImPlot | FetchContent | MIT |
