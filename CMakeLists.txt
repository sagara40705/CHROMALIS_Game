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

add_library(ChromalisAssets STATIC src/assets/StaticAsset.cpp src/assets/SkeletalAsset.cpp)
target_include_directories(ChromalisAssets PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(ChromalisAssets PUBLIC cxx_std_20)
target_compile_definitions(ChromalisAssets PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(ChromalisAssets PRIVATE /W4 /WX /permissive- /utf-8 /EHsc)
target_link_libraries(ChromalisRuntime PUBLIC ChromalisAssets)
include(cmake/Animation.cmake)
include(cmake/Vfx.cmake)
include(cmake/RuntimeLicenses.cmake)
include(cmake/Scene.cmake)
include(cmake/Physics.cmake)
set(CHROMALIS_RUNTIME_LICENSE_DIR "${CHROMALIS_RUNTIME_LICENSE_DIR}" CACHE INTERNAL "Generated runtime notices")
target_sources(ChromalisRuntime PRIVATE src/runtime/VfxPlayer.cpp)
target_link_libraries(ChromalisRuntime PRIVATE EffekseerRendererDX12)
add_library(ChromalisAnimation STATIC src/animation/AnimationPlayer.cpp)
target_include_directories(ChromalisAnimation PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_options(ChromalisAnimation PRIVATE /W4 /WX /permissive- /utf-8 /EHsc)
target_compile_definitions(ChromalisAnimation PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(ChromalisAnimation PUBLIC ChromalisAssets PRIVATE ozz_animation_offline)
target_link_libraries(ChromalisRuntime PUBLIC ChromalisAnimation)
target_link_libraries(ChromalisRuntime PUBLIC ChromalisScene)
target_sources(ChromalisRuntime PRIVATE src/scene/SceneRuntime.cpp)
add_custom_command(OUTPUT "${SHADER_DIR}/Skin_vs.h"
  COMMAND "${DXC_EXECUTABLE}" -T vs_6_0 -E vs_main -D SKINNED -WX -Ges -Zpr
    -Fh "${SHADER_DIR}/Skin_vs.h" -Vn Skin_vs "${CMAKE_CURRENT_SOURCE_DIR}/shaders/Mesh.hlsl"
  DEPENDS shaders/Mesh.hlsl VERBATIM)
target_sources(ChromalisRuntime PRIVATE "${SHADER_DIR}/Skin_vs.h")
add_custom_command(OUTPUT "${SHADER_DIR}/Shadow_ps.h"
  COMMAND "${DXC_EXECUTABLE}" -T ps_6_0 -E shadow_ps -WX -Ges -Zpr
    -Fh "${SHADER_DIR}/Shadow_ps.h" -Vn Shadow_ps "${CMAKE_CURRENT_SOURCE_DIR}/shaders/Mesh.hlsl"
  DEPENDS shaders/Mesh.hlsl VERBATIM)
target_sources(ChromalisRuntime PRIVATE "${SHADER_DIR}/Shadow_ps.h")
foreach(STAGE IN ITEMS vs ps)
  add_custom_command(OUTPUT "${SHADER_DIR}/Mesh_${STAGE}.h"
    COMMAND "${DXC_EXECUTABLE}" -T ${STAGE}_6_0 -E ${STAGE}_main -WX -Ges -Zpr
      -Fh "${SHADER_DIR}/Mesh_${STAGE}.h" -Vn Mesh_${STAGE} "${CMAKE_CURRENT_SOURCE_DIR}/shaders/Mesh.hlsl"
    DEPENDS shaders/Mesh.hlsl VERBATIM)
  target_sources(ChromalisRuntime PRIVATE "${SHADER_DIR}/Mesh_${STAGE}.h")
endforeach()

option(CHROMALIS_BUILD_COOKER "Build offline glTF/GLB model cooker" ON)
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

target_sources(ChromalisRuntime PRIVATE src/gameplay/ScenePlayback.cpp src/gameplay/PlayerMotion.cpp)
include(cmake/Behaviours.cmake)

add_executable(ChromalisGame WIN32 Main.cpp app.manifest)
chromalis_copy_runtime_licenses(ChromalisGame)
target_link_libraries(ChromalisGame PRIVATE ChromalisRuntime)
target_compile_options(ChromalisGame PRIVATE /W4 /WX /permissive- /utf-8 /EHsc)
set_target_properties(ChromalisGame PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:ChromalisGame>")

if(BUILD_TESTING)
  add_executable(ChromalisPhysicsTests tests/PhysicsTests.cpp)
  target_link_libraries(ChromalisPhysicsTests PRIVATE ChromalisPhysics)
  target_compile_options(ChromalisPhysicsTests PRIVATE /W4 /WX /permissive- /utf-8 /EHsc)
  add_test(NAME physics.cpu COMMAND ChromalisPhysicsTests "${CMAKE_CURRENT_BINARY_DIR}/physics-tests")
  add_executable(ChromalisBehaviourTests tests/BehaviourTests.cpp)
  target_link_libraries(ChromalisBehaviourTests PRIVATE ChromalisRuntime)
  target_compile_options(ChromalisBehaviourTests PRIVATE /W4 /WX /permissive- /utf-8 /EHsc)
  add_test(NAME behaviour.warp COMMAND ChromalisBehaviourTests "${CMAKE_CURRENT_BINARY_DIR}/behaviour-tests")
  set_tests_properties(behaviour.warp PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
  add_executable(ChromalisScenePlaybackTests tests/ScenePlaybackTests.cpp)
  target_include_directories(ChromalisScenePlaybackTests PRIVATE "${CHROMALIS_BEHAVIOUR_DIR}")
  target_link_libraries(ChromalisScenePlaybackTests PRIVATE ChromalisRuntime)
  target_compile_options(ChromalisScenePlaybackTests PRIVATE /W4 /WX /permissive- /utf-8 /EHsc)
  add_test(NAME player.scene COMMAND ChromalisScenePlaybackTests "${CMAKE_CURRENT_BINARY_DIR}/player-tests" warp)
  set_tests_properties(player.scene PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
  add_executable(ChromalisPlayerMotionTests tests/PlayerMotionTests.cpp src/gameplay/PlayerMotion.cpp)
  target_link_libraries(ChromalisPlayerMotionTests PRIVATE ChromalisScene)
  target_compile_features(ChromalisPlayerMotionTests PRIVATE cxx_std_20)
  target_compile_options(ChromalisPlayerMotionTests PRIVATE /W4 /WX /permissive- /utf-8 /EHsc)
  add_test(NAME player.motion COMMAND ChromalisPlayerMotionTests)
  add_executable(ChromalisMaterialTests tests/MaterialTests.cpp)
  target_link_libraries(ChromalisMaterialTests PRIVATE ChromalisRuntime)
  target_compile_options(ChromalisMaterialTests PRIVATE /W4 /WX /permissive- /utf-8 /EHsc)
  add_test(NAME material.warp COMMAND ChromalisMaterialTests "${CMAKE_CURRENT_BINARY_DIR}/material-tests" warp)
  set_tests_properties(material.warp PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
  add_executable(ChromalisSceneTests tests/SceneTests.cpp)
  target_link_libraries(ChromalisSceneTests PRIVATE ChromalisScene)
  target_compile_options(ChromalisSceneTests PRIVATE /W4 /WX /permissive- /utf-8 /EHsc)
  add_test(NAME scene.cpu COMMAND ChromalisSceneTests "${CMAKE_CURRENT_BINARY_DIR}/scene-tests/cpu")
  if(CHROMALIS_BUILD_COOKER)
    add_executable(ChromalisSceneRuntimeTests tests/SceneRuntimeTests.cpp)
    add_executable(ChromalisShadowTests tests/ShadowTests.cpp)
    target_link_libraries(ChromalisShadowTests PRIVATE ChromalisRuntime)
    target_compile_options(ChromalisShadowTests PRIVATE /W4 /WX /permissive- /utf-8 /EHsc)
    add_test(NAME shadow.warp COMMAND ChromalisShadowTests "${CMAKE_CURRENT_BINARY_DIR}/shadow-tests" warp)
    set_tests_properties(shadow.warp PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
    target_link_libraries(ChromalisSceneRuntimeTests PRIVATE ChromalisRuntime)
    target_compile_options(ChromalisSceneRuntimeTests PRIVATE /W4 /WX /permissive- /utf-8 /EHsc)
    add_test(NAME scene.warp COMMAND ChromalisSceneRuntimeTests "${CMAKE_CURRENT_BINARY_DIR}/scene-tests/warp"
      "${CMAKE_CURRENT_BINARY_DIR}/asset-tests/cube.cmesh" "${CMAKE_CURRENT_BINARY_DIR}/animation-tests/figure.cskin"
      "${CMAKE_CURRENT_BINARY_DIR}/animation-tests/skinning.cskin" "${CMAKE_CURRENT_SOURCE_DIR}/samples/Assets/Vfx/Pulse.efkefc" warp)
    set_tests_properties(scene.warp PROPERTIES FIXTURES_REQUIRED "animated-assets;cooked-assets" TIMEOUT 120 RUN_SERIAL TRUE)
    add_executable(ChromalisAnimationTests tests/AnimationTests.cpp)
    target_link_libraries(ChromalisAnimationTests PRIVATE ChromalisCook ChromalisAnimation)
    target_compile_options(ChromalisAnimationTests PRIVATE /W4 /WX /permissive- /utf-8 /EHsc)
    add_test(NAME animation.cpu COMMAND ChromalisAnimationTests "${CMAKE_CURRENT_SOURCE_DIR}/samples/Assets/RiggedFigure/RiggedFigure.glb" "${CMAKE_CURRENT_BINARY_DIR}/animation-tests/figure.cskin")
    set_tests_properties(animation.cpu PROPERTIES FIXTURES_SETUP animated-assets TIMEOUT 30)
    add_test(NAME animation.warp COMMAND ChromalisGame --warp --require-debug --animation "${CMAKE_CURRENT_BINARY_DIR}/animation-tests/figure.cskin" --frames 60 --capture "${CMAKE_CURRENT_BINARY_DIR}/animation-tests/figure.bmp")
    set_tests_properties(animation.warp PROPERTIES FIXTURES_REQUIRED animated-assets TIMEOUT 60 RUN_SERIAL TRUE)
    add_test(NAME animation.warp.bind COMMAND ChromalisGame --warp --require-debug --bind-pose
      --animation "${CMAKE_CURRENT_BINARY_DIR}/animation-tests/figure.cskin" --frames 4 --capture "${CMAKE_CURRENT_BINARY_DIR}/animation-tests/bind.bmp")
    set_tests_properties(animation.warp.bind PROPERTIES FIXTURES_REQUIRED animated-assets TIMEOUT 60 RUN_SERIAL TRUE)
    add_test(NAME animation.warp.pixels COMMAND ChromalisGame --warp --require-debug --animation-check
      --animation "${CMAKE_CURRENT_BINARY_DIR}/animation-tests/skinning.cskin" --capture "${CMAKE_CURRENT_BINARY_DIR}/animation-tests/skinning.bmp")
    set_tests_properties(animation.warp.pixels PROPERTIES FIXTURES_REQUIRED animated-assets TIMEOUT 60 RUN_SERIAL TRUE)
    add_test(NAME vfx.warp.pixels COMMAND ChromalisGame --warp --require-debug --vfx-check
      --vfx "${CMAKE_CURRENT_SOURCE_DIR}/samples/Assets/Vfx/AlphaProbe.efkefc" --capture "${CMAKE_CURRENT_BINARY_DIR}/vfx-alpha.bmp")
    set_tests_properties(vfx.warp.pixels PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
    add_test(NAME vfx.warp.changed-retained COMMAND ChromalisGame --warp --require-debug --vfx-check
      --vfx "${CMAKE_CURRENT_SOURCE_DIR}/samples/Assets/Vfx/Pulse.efkefc" --vfx-update "${CMAKE_CURRENT_SOURCE_DIR}/samples/Assets/Vfx/AlphaProbe.efkefc"
      --capture "${CMAKE_CURRENT_BINARY_DIR}/vfx-changed-retained.bmp")
    set_tests_properties(vfx.warp.changed-retained PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
    add_test(NAME vfx.warp.depth COMMAND ChromalisGame --warp --require-debug --vfx-depth-check
      --asset "${CMAKE_CURRENT_BINARY_DIR}/asset-tests/midtone.cmesh" --vfx "${CMAKE_CURRENT_SOURCE_DIR}/samples/Assets/Vfx/AlphaProbe.efkefc"
      --capture "${CMAKE_CURRENT_BINARY_DIR}/vfx-depth.bmp")
    set_tests_properties(vfx.warp.depth PROPERTIES FIXTURES_REQUIRED cooked-assets TIMEOUT 60 RUN_SERIAL TRUE)
    add_test(NAME integration.warp COMMAND ChromalisGame --warp --require-debug --integration-smoke
      --animation "${CMAKE_CURRENT_BINARY_DIR}/animation-tests/figure.cskin" --asset "${CMAKE_CURRENT_BINARY_DIR}/asset-tests/cube.cmesh"
      --vfx "${CMAKE_CURRENT_SOURCE_DIR}/samples/Assets/Vfx/Pulse.efkefc" --capture "${CMAKE_CURRENT_BINARY_DIR}/integration.bmp")
    set_tests_properties(integration.warp PROPERTIES FIXTURES_REQUIRED "animated-assets;cooked-assets" TIMEOUT 60 RUN_SERIAL TRUE)
    add_executable(ChromalisAssetTests tests/AssetTests.cpp)
    target_link_libraries(ChromalisAssetTests PRIVATE ChromalisCook windowscodecs ole32)
    target_compile_definitions(ChromalisAssetTests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(ChromalisAssetTests PRIVATE /W4 /WX /permissive- /utf-8 /EHsc)
    add_test(NAME assets.cpu COMMAND ChromalisAssetTests "${CMAKE_CURRENT_SOURCE_DIR}/samples/Assets/Cube/Cube.gltf" "${CMAKE_CURRENT_BINARY_DIR}/asset-tests")
    set_tests_properties(assets.cpu PROPERTIES FIXTURES_SETUP cooked-assets TIMEOUT 30)
    add_test(NAME assets.cooker.paths COMMAND powershell -NoProfile -ExecutionPolicy Bypass
      -File "${CMAKE_CURRENT_SOURCE_DIR}/tests/CookerCliTests.ps1" -Cooker "$<TARGET_FILE:ChromalisAssetCooker>"
      -Source "${CMAKE_CURRENT_SOURCE_DIR}/samples/Assets/Cube" -Root "${CMAKE_CURRENT_BINARY_DIR}/report-tests")
    set_tests_properties(assets.cooker.paths PROPERTIES TIMEOUT 60)
    add_test(NAME assets.warp.reload COMMAND ChromalisGame --warp --require-debug --asset-smoke
      --asset "${CMAKE_CURRENT_BINARY_DIR}/asset-tests/cube.cmesh" --capture "${CMAKE_CURRENT_BINARY_DIR}/asset-tests/reload.bmp")
    set_tests_properties(assets.warp.reload PROPERTIES FIXTURES_REQUIRED cooked-assets TIMEOUT 60 RUN_SERIAL TRUE)
    add_test(NAME assets.warp.pixels COMMAND ChromalisGame --warp --require-debug --asset-check
      --asset "${CMAKE_CURRENT_BINARY_DIR}/asset-tests/scene.cmesh" --capture "${CMAKE_CURRENT_BINARY_DIR}/asset-tests/pixels.bmp")
    set_tests_properties(assets.warp.pixels PROPERTIES FIXTURES_REQUIRED cooked-assets TIMEOUT 60 RUN_SERIAL TRUE)
    add_test(NAME assets.warp.srgb COMMAND ChromalisGame --warp --require-debug --color-check
      --asset "${CMAKE_CURRENT_BINARY_DIR}/asset-tests/midtone.cmesh" --capture "${CMAKE_CURRENT_BINARY_DIR}/asset-tests/midtone.bmp")
    set_tests_properties(assets.warp.srgb PROPERTIES FIXTURES_REQUIRED cooked-assets TIMEOUT 60 RUN_SERIAL TRUE)
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

# Run in each project's directory: source_group is directory-scoped.
function(chromalis_group_project_sources)
  get_property(project_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
  foreach(project_target IN LISTS project_targets)
    if(NOT project_target MATCHES "^Chromalis" OR project_target MATCHES "^Chromalis(ImGui|Guizmo|Cgltf)$")
      continue()
    endif()
    get_target_property(project_sources "${project_target}" SOURCES)
    set(local_sources)
    set(header_directories)
    foreach(project_source IN LISTS project_sources)
      if(project_source MATCHES "^\\$<")
        continue()
      endif()
      get_filename_component(absolute_source "${project_source}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
      cmake_path(IS_PREFIX CMAKE_CURRENT_SOURCE_DIR "${absolute_source}" NORMALIZE is_local)
      if(is_local)
        list(APPEND local_sources "${absolute_source}")
        if(absolute_source MATCHES "\\.cpp$")
          get_filename_component(header_directory "${absolute_source}" DIRECTORY)
          list(APPEND header_directories "${header_directory}")
        endif()
      elseif(absolute_source MATCHES "/generated/")
        source_group("Generated/Shaders" FILES "${absolute_source}")
      endif()
    endforeach()
    list(REMOVE_DUPLICATES header_directories)
    foreach(header_directory IN LISTS header_directories)
      file(GLOB project_headers CONFIGURE_DEPENDS "${header_directory}/*.h" "${header_directory}/*.hpp")
      if(project_headers)
        target_sources("${project_target}" PRIVATE ${project_headers})
        list(APPEND local_sources ${project_headers})
      endif()
    endforeach()
    if(local_sources)
      source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${local_sources})
    endif()
  endforeach()
  source_group("Build/CMake" REGULAR_EXPRESSION "(\\.rule$|CMakeLists\\.txt$)")
endfunction()
chromalis_group_project_sources()
