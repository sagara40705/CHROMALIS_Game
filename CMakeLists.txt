cmake_minimum_required(VERSION 4.2)
project(CHROMALIS_Game VERSION 0.0.1 LANGUAGES CXX)
if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "Phase 0A requires Windows x64 and the MSVC toolchain.")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(PROJECT_IS_TOP_LEVEL)
  include(CTest)
endif()

# Use the compiler from the same pinned Windows SDK as the headers/libraries.
get_filename_component(WINDOWS_KITS_ROOT "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows Kits\\Installed Roots;KitsRoot10]" ABSOLUTE)
find_program(DXC_EXECUTABLE dxc HINTS
  "${WINDOWS_KITS_ROOT}/bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/x64" NO_DEFAULT_PATH REQUIRED)
execute_process(COMMAND "${DXC_EXECUTABLE}" --version OUTPUT_VARIABLE DXC_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE)
message(STATUS "Shader compiler: ${DXC_EXECUTABLE} (${DXC_VERSION})")
set(SHADER_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${SHADER_DIR}")
foreach(STAGE IN ITEMS vs ps)
  add_custom_command(OUTPUT "${SHADER_DIR}/Triangle_${STAGE}.h"
    COMMAND "${DXC_EXECUTABLE}" -T ${STAGE}_6_0 -E ${STAGE}_main -WX -Ges -Zpr
      -Fh "${SHADER_DIR}/Triangle_${STAGE}.h" -Vn Triangle_${STAGE}
      "${CMAKE_CURRENT_SOURCE_DIR}/shaders/Triangle.hlsl"
    DEPENDS shaders/Triangle.hlsl VERBATIM)
endforeach()

add_library(ChromalisRuntime STATIC
  src/runtime/Diagnostics.cpp src/runtime/Renderer.cpp src/runtime/Application.cpp
  "${SHADER_DIR}/Triangle_vs.h" "${SHADER_DIR}/Triangle_ps.h")
target_include_directories(ChromalisRuntime PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src" PRIVATE "${SHADER_DIR}")
target_compile_features(ChromalisRuntime PUBLIC cxx_std_20)
target_compile_definitions(ChromalisRuntime PUBLIC UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(ChromalisRuntime PRIVATE /W4 /WX /permissive- /utf-8 /EHsc)
target_link_libraries(ChromalisRuntime PUBLIC d3d12 dxgi dxguid user32 shell32)

add_executable(ChromalisGame WIN32 Main.cpp app.manifest)
target_link_libraries(ChromalisGame PRIVATE ChromalisRuntime)
target_compile_options(ChromalisGame PRIVATE /W4 /WX /permissive- /utf-8 /EHsc)
set_target_properties(ChromalisGame PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:ChromalisGame>")

if(BUILD_TESTING)
  # WARP is explicit: it verifies correctness, never a hardware performance claim.
  add_test(NAME game.warp.smoke COMMAND ChromalisGame --warp --smoke-test --require-debug
    --capture "${CMAKE_CURRENT_BINARY_DIR}/game-smoke.bmp")
  set_tests_properties(game.warp.smoke PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
endif()
if(PROJECT_IS_TOP_LEVEL)
  set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT ChromalisGame)
endif()
