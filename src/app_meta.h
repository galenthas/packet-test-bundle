#pragma once
#include "version.h"

namespace appmeta {

inline constexpr const wchar_t* kWindowClassName = L"PacketTestBundleWindow";
inline constexpr const wchar_t* kWindowTitle = L"Packet Test Bundle " VERSION_STRING_W;
inline constexpr const char* kWindowTitleUtf8 = "Packet Test Bundle " VERSION_STRING;
inline constexpr const char* kVersionUtf8 = VERSION_STRING;

}
