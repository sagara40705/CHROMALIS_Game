include(FetchContent)
set(CHROMALIS_JOLT_ARCHIVE "https://codeload.github.com/jrouwe/JoltPhysics/tar.gz/refs/tags/v5.5.0" CACHE STRING "Jolt 5.5.0 archive URL or local path")
function(chromalis_add_jolt)
  # Joltの設定をこのスコープに閉じ、親のコンパイラ設定を変更しない。
  foreach(option IN ITEMS OVERRIDE_CXX_FLAGS INTERPROCEDURAL_OPTIMIZATION ENABLE_ALL_WARNINGS
      USE_STATIC_MSVC_RUNTIME_LIBRARY FLOATING_POINT_EXCEPTIONS_ENABLED ENABLE_INSTALL
      DEBUG_RENDERER_IN_DEBUG_AND_RELEASE PROFILER_IN_DEBUG_AND_RELEASE
      USE_SSE4_1 USE_SSE4_2 USE_AVX USE_AVX2 USE_AVX512 USE_LZCNT USE_TZCNT USE_F16C USE_FMADD
      TARGET_UNIT_TESTS TARGET_HELLO_WORLD TARGET_PERFORMANCE_TEST TARGET_SAMPLES TARGET_VIEWER)
    set(${option} OFF)
  endforeach()
  set(BUILD_SHARED_LIBS OFF)
  FetchContent_Declare(jolt URL "${CHROMALIS_JOLT_ARCHIVE}"
    URL_HASH SHA256=3dae862a32c9092fca5b17f8e5d32cd57e035d30c3145c00040f13ca58a866df SOURCE_SUBDIR Build)
  FetchContent_MakeAvailable(jolt)
  set(jolt_SOURCE_DIR "${jolt_SOURCE_DIR}" PARENT_SCOPE)
endfunction()
chromalis_add_jolt()
add_library(ChromalisPhysics STATIC src/physics/PhysicsWorld.cpp)
target_include_directories(ChromalisPhysics PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_include_directories(ChromalisPhysics SYSTEM PRIVATE "${jolt_SOURCE_DIR}")
target_compile_definitions(ChromalisPhysics PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(ChromalisPhysics PRIVATE /W4 /WX /permissive- /utf-8 /EHsc)
target_link_libraries(ChromalisPhysics PUBLIC ChromalisScene PRIVATE Jolt)
target_link_libraries(ChromalisRuntime PUBLIC ChromalisPhysics)
configure_file("${jolt_SOURCE_DIR}/LICENSE" "${CHROMALIS_RUNTIME_LICENSE_DIR}/JoltPhysics.txt" COPYONLY)
