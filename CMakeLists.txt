cmake_minimum_required(VERSION 3.20)
project(PacketTestBundle CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)

include(FetchContent)

set(WIRESHARK_ROOT "" CACHE PATH "Optional Wireshark installation root used to supplement portable tshark bundle")
if(NOT WIRESHARK_ROOT)
    if(EXISTS "C:/Program Files/Wireshark")
        set(WIRESHARK_ROOT "C:/Program Files/Wireshark")
    elseif(EXISTS "C:/Program Files (x86)/Wireshark")
        set(WIRESHARK_ROOT "C:/Program Files (x86)/Wireshark")
    endif()
endif()

# Dear ImGui v1.91.5
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.5
)
FetchContent_MakeAvailable(imgui)

# ImPlot v0.17
FetchContent_Declare(
    implot
    GIT_REPOSITORY https://github.com/epezent/implot.git
    GIT_TAG        v0.17
)
FetchContent_MakeAvailable(implot)

set(IMGUI_DIR ${imgui_SOURCE_DIR})
set(IMPLOT_DIR ${implot_SOURCE_DIR})

set(SOURCES
    src/main.cpp
    src/app.cpp
    src/process_util.cpp
    src/parser_iperf.cpp
    src/command_builder.cpp
    src/worker_iperf.cpp
    src/worker_ping.cpp
    src/worker_pcap.cpp
    resources/app_icon.rc

    # ImGui core
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp

    # ImPlot
    ${IMPLOT_DIR}/implot.cpp
    ${IMPLOT_DIR}/implot_items.cpp
)

add_executable(PacketTestBundle WIN32 ${SOURCES})
set_target_properties(PacketTestBundle PROPERTIES OUTPUT_NAME "PacketTestBundle")

target_include_directories(PacketTestBundle PRIVATE
    src
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${IMPLOT_DIR}
)

target_compile_definitions(PacketTestBundle PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    WINVER=0x0600
    _WIN32_WINNT=0x0600
)

target_compile_options(PacketTestBundle PRIVATE
    $<$<CONFIG:Release>:/O2>
    $<$<CONFIG:Release>:/Gy>
    $<$<CONFIG:Release>:/GL>
)

target_link_options(PacketTestBundle PRIVATE
    $<$<CONFIG:Release>:/OPT:REF>
    $<$<CONFIG:Release>:/OPT:ICF>
    $<$<CONFIG:Release>:/LTCG>

)

target_link_libraries(PacketTestBundle PRIVATE
    d3d11
    dxgi
    d3dcompiler
    iphlpapi
    ws2_32
)

set(_msvc_runtime_dir "$ENV{VCToolsRedistDir}x64/Microsoft.VC143.CRT")
foreach(_runtime_name IN ITEMS msvcp140.dll vcruntime140.dll vcruntime140_1.dll)
    string(REPLACE "." "_" _runtime_var "${_runtime_name}")
    find_file(_runtime_path_${_runtime_var}
        NAMES ${_runtime_name}
        HINTS "${_msvc_runtime_dir}"
    )
    if(_runtime_path_${_runtime_var})
        install(FILES "${_runtime_path_${_runtime_var}}" DESTINATION .)
    endif()
endforeach()

# Copy bin/ directory to output directory after build
add_custom_command(TARGET PacketTestBundle POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/../bin"
        "$<TARGET_FILE_DIR:PacketTestBundle>/bin"
    COMMENT "Copying bin/ directory to output"
    VERBATIM
)

if(WIRESHARK_ROOT AND EXISTS "${WIRESHARK_ROOT}/dumpcap.exe")
    add_custom_command(TARGET PacketTestBundle POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${WIRESHARK_ROOT}/dumpcap.exe"
            "$<TARGET_FILE_DIR:PacketTestBundle>/bin/tshark/dumpcap.exe"
        COMMENT "Copying dumpcap.exe into bundled tshark directory"
        VERBATIM
    )
endif()

if(WIRESHARK_ROOT AND EXISTS "${WIRESHARK_ROOT}/extcap")
    add_custom_command(TARGET PacketTestBundle POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${WIRESHARK_ROOT}/extcap"
            "$<TARGET_FILE_DIR:PacketTestBundle>/bin/tshark/extcap"
        COMMENT "Copying extcap helpers into bundled tshark directory"
        VERBATIM
    )
endif()

install(TARGETS PacketTestBundle
    RUNTIME DESTINATION .
)

install(DIRECTORY "${CMAKE_SOURCE_DIR}/../bin"
    DESTINATION .
)

if(WIRESHARK_ROOT AND EXISTS "${WIRESHARK_ROOT}/dumpcap.exe")
    install(FILES "${WIRESHARK_ROOT}/dumpcap.exe"
        DESTINATION "bin/tshark"
    )
endif()

if(WIRESHARK_ROOT AND EXISTS "${WIRESHARK_ROOT}/extcap")
    install(DIRECTORY "${WIRESHARK_ROOT}/extcap"
        DESTINATION "bin/tshark"
    )
endif()

install(FILES "${CMAKE_SOURCE_DIR}/PORTABLE_README.txt"
    DESTINATION .
)

# ── Unit tests ────────────────────────────────────────────────────────────────
add_executable(PacketTestBundleTests
    test/test_main.cpp
    src/command_builder.cpp
    src/parser_iperf.cpp
)
target_include_directories(PacketTestBundleTests PRIVATE src)
target_compile_definitions(PacketTestBundleTests PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
    WINVER=0x0600 _WIN32_WINNT=0x0600
)
set_target_properties(PacketTestBundleTests PROPERTIES OUTPUT_NAME "PacketTestBundleTests")

add_custom_target(run_tests
    COMMAND PacketTestBundleTests
    DEPENDS PacketTestBundleTests
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running unit tests"
)

set(CPACK_PACKAGE_NAME "Packet Test Bundle")
set(CPACK_PACKAGE_VENDOR "Local")
set(CPACK_PACKAGE_FILE_NAME "Packet-Test-Bundle-portable-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "PacketTestBundle")
set(CPACK_MONOLITHIC_INSTALL ON)
set(CPACK_GENERATOR "ZIP")

include(CPack)
