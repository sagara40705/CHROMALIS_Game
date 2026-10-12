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
  src/runtime/Diagnostics.cpp src/runtime/Renderer.cpp src/runtime/RendererAssets.cpp src/runtime/Application.cpp
  "${SHADER_DIR}/Triangle_vs.h" "${SHADER_DIR}/Triangle_ps.h")
target_include_directories(ChromalisRuntime PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src" PRIVATE "${SHADER_DIR}")
target_compile_features(ChromalisRuntime PUBLIC cxx_std_20)
target_compile_definitions(ChromalisRuntime PUBLIC UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(ChromalisRuntime PRIVATE /W4 /WX /permissive- /utf-8 /EHsc)
target_link_libraries(ChromalisRuntime PUBLIC d3d12 dxgi dxguid user32 shell32)

add_library(ChromalisAssets STATIC src/assets/StaticAsset.cpp)
target_include_directories(ChromalisAssets PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(ChromalisAssets PUBLIC cxx_std_20)
target_compile_definitions(ChromalisAssets PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(ChromalisAssets PRIVATE /W4 /WX /permissive- /utf-8 /EHsc)
target_link_libraries(ChromalisRuntime PUBLIC ChromalisAssets)
foreach(STAGE IN ITEMS vs ps)
  add_custom_command(OUTPUT "${SHADER_DIR}/Mesh_${STAGE}.h"
    COMMAND "${DXC_EXECUTABLE}" -T ${STAGE}_6_0 -E ${STAGE}_main -WX -Ges -Zpr
      -Fh "${SHADER_DIR}/Mesh_${STAGE}.h" -Vn Mesh_${STAGE} "${CMAKE_CURRENT_SOURCE_DIR}/shaders/Mesh.hlsl"
    DEPENDS shaders/Mesh.hlsl VERBATIM)
  target_sources(ChromalisRuntime PRIVATE "${SHADER_DIR}/Mesh_${STAGE}.h")
endforeach()

option(CHROMALIS_BUILD_COOKER "Build offline glTF/GLB static model cooker" ON)
if(CHROMALIS_BUILD_COOKER)
  include(FetchContent)
  set(CHROMALIS_CGLTF_ARCHIVE "https://github.com/jkuhlmann/cgltf/archive/refs/tags/v1.15.tar.gz" CACHE STRING "Pinned cgltf archive URL or local path")
  FetchContent_Declare(cgltf URL "${CHROMALIS_CGLTF_ARCHIVE}"
    URL_HASH SHA256=84e352092e5cd6aab7f66de62ddb66beb5e6f18d412ca9d12950d7a55bfef25a)
  FetchContent_MakeAvailable(cgltf)
  add_library(ChromalisCgltf STATIC src/cooker/Cgltf.cpp)
  target_include_directories(ChromalisCgltf SYSTEM PUBLIC ${cgltf_SOURCE_DIR})
  add_library(ChromalisCook STATIC src/cooker/CookStatic.cpp)
  target_link_libraries(ChromalisCook PUBLIC ChromalisAssets PRIVATE ChromalisCgltf windowscodecs ole32 bcrypt crypt32)
  target_compile_definitions(ChromalisCook PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_compile_options(ChromalisCook PRIVATE /W4 /WX /permissive- /utf-8 /EHsc)
  add_executable(ChromalisAssetCooker src/cooker/Main.cpp)
  target_link_libraries(ChromalisAssetCooker PRIVATE ChromalisCook)
  target_compile_options(ChromalisAssetCooker PRIVATE /W4 /WX /permissive- /utf-8 /EHsc)
  set_target_properties(ChromalisAssetCooker PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
  add_custom_command(TARGET ChromalisAssetCooker POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:ChromalisAssetCooker>/licenses"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${cgltf_SOURCE_DIR}/LICENSE" "$<TARGET_FILE_DIR:ChromalisAssetCooker>/licenses/cgltf.txt" VERBATIM)
endif()

add_executable(ChromalisGame WIN32 Main.cpp app.manifest)
target_link_libraries(ChromalisGame PRIVATE ChromalisRuntime)
target_compile_options(ChromalisGame PRIVATE /W4 /WX /permissive- /utf-8 /EHsc)
set_target_properties(ChromalisGame PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:ChromalisGame>")

if(BUILD_TESTING)
  if(CHROMALIS_BUILD_COOKER)
    add_executable(ChromalisAssetTests src/tests/AssetTests.cpp)
    target_link_libraries(ChromalisAssetTests PRIVATE ChromalisCook windowscodecs ole32)
    target_compile_definitions(ChromalisAssetTests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(ChromalisAssetTests PRIVATE /W4 /WX /permissive- /utf-8 /EHsc)
    add_test(NAME assets.cpu COMMAND ChromalisAssetTests "${CMAKE_CURRENT_SOURCE_DIR}/assets/Cube/Cube.gltf" "${CMAKE_CURRENT_BINARY_DIR}/asset-tests")
    set_tests_properties(assets.cpu PROPERTIES FIXTURES_SETUP cooked-assets TIMEOUT 30)
    add_test(NAME assets.warp.reload COMMAND ChromalisGame --warp --require-debug --asset-smoke
      --asset "${CMAKE_CURRENT_BINARY_DIR}/asset-tests/cube.cmesh" --capture "${CMAKE_CURRENT_BINARY_DIR}/asset-tests/reload.bmp")
    set_tests_properties(assets.warp.reload PROPERTIES FIXTURES_REQUIRED cooked-assets TIMEOUT 60 RUN_SERIAL TRUE)
    add_test(NAME assets.warp.pixels COMMAND ChromalisGame --warp --require-debug --asset-check
      --asset "${CMAKE_CURRENT_BINARY_DIR}/asset-tests/scene.cmesh" --capture "${CMAKE_CURRENT_BINARY_DIR}/asset-tests/pixels.bmp")
    set_tests_properties(assets.warp.pixels PROPERTIES FIXTURES_REQUIRED cooked-assets TIMEOUT 60 RUN_SERIAL TRUE)
    add_test(NAME assets.warp.changed-retained COMMAND ChromalisGame --warp --require-debug --asset-check
      --asset "${CMAKE_CURRENT_BINARY_DIR}/asset-tests/scene.cmesh" --asset-update "${CMAKE_CURRENT_BINARY_DIR}/asset-tests/changed.cmesh"
      --capture "${CMAKE_CURRENT_BINARY_DIR}/asset-tests/changed-retained.bmp")
    set_tests_properties(assets.warp.changed-retained PROPERTIES FIXTURES_REQUIRED cooked-assets TIMEOUT 60 RUN_SERIAL TRUE)
  endif()
  # WARP is explicit: it verifies correctness, never a hardware performance claim.
  add_test(NAME game.warp.smoke COMMAND ChromalisGame --warp --smoke-test --require-debug
    --capture "${CMAKE_CURRENT_BINARY_DIR}/game-smoke.bmp")
  set_tests_properties(game.warp.smoke PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
endif()
if(PROJECT_IS_TOP_LEVEL)
  set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT ChromalisGame)
endif()
