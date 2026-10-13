# 固定した配布アーカイブ内の利用条件を実行ファイルに添付する。
set(CHROMALIS_RUNTIME_LICENSE_DIR "${CMAKE_CURRENT_BINARY_DIR}/runtime-licenses")
file(MAKE_DIRECTORY "${CHROMALIS_RUNTIME_LICENSE_DIR}")
configure_file("${ozz_SOURCE_DIR}/LICENSE.md" "${CHROMALIS_RUNTIME_LICENSE_DIR}/ozz-animation.txt" COPYONLY)
configure_file("${effekseer_SOURCE_DIR}/LICENSE.txt" "${CHROMALIS_RUNTIME_LICENSE_DIR}/Effekseer.txt" COPYONLY)
configure_file("${effekseer_SOURCE_DIR}/LICENSE_RUNTIME_DIRECTX.txt" "${CHROMALIS_RUNTIME_LICENSE_DIR}/Effekseer-DirectX.txt" COPYONLY)
configure_file("${effekseer_SOURCE_DIR}/src/3rdParty/LLGI/LICENSE" "${CHROMALIS_RUNTIME_LICENSE_DIR}/LLGI.txt" COPYONLY)
file(READ "${effekseer_SOURCE_DIR}/src/3rdParty/stb_effekseer/stb_image_effekseer.h" stb_source)
string(FIND "${stb_source}" "This software is available under 2 licenses" stb_notice_start)
if(stb_notice_start LESS 0)
  message(FATAL_ERROR "Pinned stb_image license notice was not found")
endif()
string(SUBSTRING "${stb_source}" ${stb_notice_start} -1 stb_notice)
file(WRITE "${CHROMALIS_RUNTIME_LICENSE_DIR}/stb_image.txt" "${stb_notice}")
function(chromalis_copy_runtime_licenses target)
  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CHROMALIS_RUNTIME_LICENSE_DIR}"
      "$<TARGET_FILE_DIR:${target}>/licenses" VERBATIM)
endfunction()
