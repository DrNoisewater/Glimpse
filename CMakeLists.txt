cmake_minimum_required(VERSION 3.24)
# The one place the version lives. The Release workflow checks it matches the
# version you type when publishing, and it flows into the EXE, installer and app.
project(Glimpse VERSION 0.3.1 DESCRIPTION "Instant screen recorder" LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(CTest)
if(BUILD_TESTING)
  add_executable(policy_tests tests/policy_tests.cpp)
  target_include_directories(policy_tests PRIVATE src)
  add_test(NAME policy_tests COMMAND policy_tests)
  add_executable(audio_tests tests/audio_tests.cpp)
  target_include_directories(audio_tests PRIVATE src)
  add_test(NAME audio_tests COMMAND audio_tests)
endif()

if(NOT WIN32)
  message(STATUS "Non-Windows host: only the portable tests are built.")
  return()
endif()
if(NOT MSVC)
  message(FATAL_ERROR "Build Glimpse with Visual Studio (MSVC) and a Windows 10/11 SDK; the SDK provides C++/WinRT.")
endif()

enable_language(RC)
set(GLIMPSE_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}")
set(GLIMPSE_GENERATED "${CMAKE_CURRENT_BINARY_DIR}/generated")
configure_file(src/version.h.in "${GLIMPSE_GENERATED}/version.h" @ONLY)
configure_file(src/app.manifest.in "${GLIMPSE_GENERATED}/app.manifest" @ONLY)
configure_file(src/resources.rc.in "${GLIMPSE_GENERATED}/resources.rc" @ONLY)

add_executable(Glimpse WIN32 src/app.cpp src/capture.cpp src/audio.cpp "${GLIMPSE_GENERATED}/resources.rc")
target_include_directories(Glimpse PRIVATE src "${GLIMPSE_GENERATED}")
target_compile_definitions(Glimpse PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
target_compile_options(Glimpse PRIVATE /W4 /permissive- /EHsc /utf-8 /Zc:__cplusplus)
# Static CRT: the EXE runs on a clean Windows 10/11 install with nothing else to install.
set_property(TARGET Glimpse PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
target_link_libraries(Glimpse PRIVATE windowsapp d3d11 dxgi mfplat mfreadwrite mfuuid gdiplus
  ole32 oleaut32 uuid shell32 shlwapi comctl32 dwmapi uxtheme wtsapi32 user32 gdi32 advapi32)
# The manifest is embedded by resources.rc; do not merge the linker's generated one.
target_link_options(Glimpse PRIVATE /MANIFEST:NO /DYNAMICBASE /NXCOMPAT /HIGHENTROPYVA)

# `cmake --install` lays out the files that ship (portable zip and installer input).
install(TARGETS Glimpse RUNTIME DESTINATION .)
install(FILES README.md DESTINATION .)
