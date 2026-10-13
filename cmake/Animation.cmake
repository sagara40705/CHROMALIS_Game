include(FetchContent)
set(CHROMALIS_OZZ_ARCHIVE "https://github.com/guillaumeblanc/ozz-animation/archive/refs/tags/0.17.0.tar.gz" CACHE STRING "ozz 0.17.0 archive URL or local path")
foreach(option IN ITEMS tools fbx gltf data samples howtos tests)
  set(ozz_build_${option} OFF CACHE BOOL "Unused ozz component" FORCE)
endforeach()
set(ozz_build_msvc_rt_dll ON CACHE BOOL "Match host CRT" FORCE)
FetchContent_Declare(ozz URL "${CHROMALIS_OZZ_ARCHIVE}"
  URL_HASH SHA256=22b4401c77a2be68acb0a8aa2e857f94dbaccfed3ab25e304a1ddea4c546e86b)
FetchContent_MakeAvailable(ozz)
