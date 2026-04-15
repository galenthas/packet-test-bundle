Packet Test Bundle Portable
===========================

This package is self-contained for normal Windows 10/11 x64 use.

Contents:
- PacketTestBundle.exe
- bin\iperf2
- bin\iperf3
- bin\tshark
- Microsoft Visual C++ runtime DLLs when required by the build

Usage:
1. Keep the whole folder together.
2. Run PacketTestBundle.exe.
3. Do not move the exe away from the bundled bin directory.

Notes:
- tshark capture may still require appropriate Windows capture permissions.
- Windows system DLLs such as DirectX and UCRT are expected to exist on supported systems.
