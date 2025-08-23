cmake_minimum_required(VERSION 3.20)
project(
  playground.zeromq
  VERSION 0.1.0.0
  LANGUAGES C CXX)

# cmake_policy(SET CMP0091 NEW) set(CMAKE_MSVC_RUNTIME_LIBRARY
# "MultiThreaded$<$<CONFIG:Debug>:Debug>")

list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)

include(CTest)
enable_testing()

find_package(Vulkan REQUIRED COMPONENTS glslang shaderc_combined)

set(nk_files "${CMAKE_CURRENT_SOURCE_DIR}/third_party/nuklear/nuklear.h"
             "${CMAKE_CURRENT_SOURCE_DIR}/third_party/nuklear/nuklear.c")
add_library(nuklear STATIC ${nk_files})

target_include_directories(
  nuklear PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/third_party/nuklear")

target_compile_definitions(
  nuklear
  PUBLIC NK_INCLUDE_FIXED_TYPES
         NK_INCLUDE_STANDARD_IO
         NK_INCLUDE_STANDARD_VARARGS
         NK_INCLUDE_DEFAULT_ALLOCATOR
         NK_INCLUDE_VERTEX_BUFFER_OUTPUT
         NK_INCLUDE_FONT_BAKING
         NK_INCLUDE_DEFAULT_FONT
         NK_UINT_DRAW_INDEX)

# download CPM.cmake
file(
  DOWNLOAD
  https://github.com/cpm-cmake/CPM.cmake/releases/download/v0.40.8/CPM.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake
  EXPECTED_HASH
    SHA256=78ba32abdf798bc616bab7c73aac32a17bbd7b06ad9e26a6add69de8f3ae4791)
include(${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake)

set(CMAKE_EXPORT_COMPILE_COMMANDS
    ON
    CACHE INTERNAL "Export compilation commands for code completion (clangd)")
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

cpmaddpackage("gh:fmtlib/fmt#11.2.0")
cpmaddpackage("gh:TartanLlama/optional@1.1.0")
cpmaddpackage("gh:TartanLlama/expected@1.1.0")
cpmaddpackage("gh:serge-sans-paille/frozen#master")
cpmaddpackage("gh:bfgroup/Lyra#develop")
cpmaddpackage("gh:odygrd/quill@7.5.0")
cpmaddpackage("gh:g-truc/glm#bf71a834948186f4097caa076cd2663c69a10e1e")
cpmaddpackage("gh:max0x7ba/atomic_queue@1.7.1")
cpmaddpackage("gh:rollbear/strong_type@15")

cpmaddpackage(
  NAME
  SDL3
  GITHUB_REPOSITORY
  libsdl-org/SDL
  GIT_TAG
  main
  OPTIONS
  "SDL_SHARED OFF"
  "SDL_STATIC ON")

cpmaddpackage(
  NAME
  zeromq
  GITHUB_REPOSITORY
  zeromq/libzmq
  GIT_TAG
  v4.3.5
  OPTIONS
  "ENABLE_DRAFTS ON")

cpmaddpackage(
  NAME
  ZppBits
  GITHUB_REPOSITORY
  eyalz800/zpp_bits
  GIT_TAG
  v4.5.1
  DOWNLOAD_ONLY
  True)

if(ZppBits_ADDED)
  add_library(ZppBits INTERFACE IMPORTED)
  target_include_directories(ZppBits INTERFACE "${ZppBits_SOURCE_DIR}")
endif()

cpmaddpackage(
  NAME
  Yas
  GITHUB_REPOSITORY
  niXman/yas
  GIT_TAG
  master
  DOWNLOAD_ONLY
  True)

if (Yas_ADDED)
  add_library(Yas INTERFACE IMPORTED)
  target_include_directories(Yas INTERFACE "${Yas_SOURCE_DIR}/include")
endif()

# cpmaddpackage( NAME spdlog GITHUB_REPOSITORY gabime/spdlog GIT_TAG v1.14.1
# OPTIONS "SPDLOG_FMT_EXTERNAL ON" OPTIONS "SPDLOG_NO_EXCEPTIONS ON")
#
# https://dhashe.com/how-to-build-highly-debuggable-c-binaries.html
add_library(global-project-compile-options-lib INTERFACE)

set(cxx_is_gcc "$<COMPILE_LANG_AND_ID:CXX,GNU>")
set(cxx_is_clang "$<COMPILE_LANG_AND_ID:CXX,Clang>")
set(cxx_is_gcc_like "$<COMPILE_LANG_AND_ID:CXX,ARMClang,AppleClang,Clang,GNU>")

if("${CMAKE_SYSTEM_NAME}" STREQUAL "Linux")
  target_compile_options(
    global-project-compile-options-lib
    INTERFACE
      $<$<CONFIG:RelWithDebInfo,Debug>:-fasynchronous-unwind-tables>
      $<$<CONFIG:RelWithDebInfo,Debug>:-ggdb3;-fno-omit-frame-pointer;-mno-omit-leaf-frame-pointer>
  )

  option(ENABLE_ASAN "Enable address sanitizer" OFF)
  if(ENABLE_ASAN)
    target_compile_options(
      global-project-compile-options-lib
      INTERFACE $<$<CONFIG:RelWithDebInfo,Debug>:-fsanitize=address)
    target_link_options(global-project-compile-options-lib INTERFACE
                        $<$<CONFIG:RelWithDebInfo,Debug>:-fsanitize=address)
  endif()

endif()

target_compile_definitions(
  global-project-compile-options-lib
  INTERFACE
    $<$<AND:$<COMPILE_LANG_AND_ID:CXX,GNU>,$<CONFIG:RelWithDebInfo,Debug>>:_GLIBCXX_ASSERTIONS>
)

# target_compile_options( global-project-compile-options-lib INTERFACE
# $<$<CXX_COMPILER_ID:MSVC>:/source-charset:utf-8;/execution-charset:utf-8>)

target_compile_definitions(
  global-project-compile-options-lib
  INTERFACE
    $<$<CONFIG:Debug>:XRAY_IS_DEBUG_BUILD>
    $<$<PLATFORM_ID:Windows>:YY_NO_UNISTD_H>
    XRAY_MATH_ENABLE_FMT_SUPPORT
    $<$<CXX_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS;_CRT_NONSTDC_NO_WARNINGS;_CRT_SECURE_NO_DEPRECATE>
)

add_custom_target(
  copy_data ALL
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/data
          ${CMAKE_BINARY_DIR}/data
  COMMENT "copy data directory"
  VERBATIM)

add_executable(
  ${CMAKE_PROJECT_NAME}
  ${PROJECT_SOURCE_DIR}/src/main.cc
  ${PROJECT_SOURCE_DIR}/src/glad.cc
  ${PROJECT_SOURCE_DIR}/src/renderer.common.hpp
  ${PROJECT_SOURCE_DIR}/src/renderer.common.cc
  ${PROJECT_SOURCE_DIR}/src/platform.window.hpp
  ${PROJECT_SOURCE_DIR}/src/platform.window.cc
  ${PROJECT_SOURCE_DIR}/src/renderer.misc.hpp
  ${PROJECT_SOURCE_DIR}/src/ui.backend.nuklear.hpp
  ${PROJECT_SOURCE_DIR}/src/ui.backend.nuklear.cc
  ${PROJECT_SOURCE_DIR}/src/error.hpp
  ${PROJECT_SOURCE_DIR}/src/ray.hpp
  ${PROJECT_SOURCE_DIR}/src/interval.hpp
  ${PROJECT_SOURCE_DIR}/src/color.hpp
  ${PROJECT_SOURCE_DIR}/src/ray.tracer.image.display.hpp
  ${PROJECT_SOURCE_DIR}/src/ray.tracer.image.display.cc
  ${PROJECT_SOURCE_DIR}/src/ray.tracer.object.defs.hpp
  ${PROJECT_SOURCE_DIR}/src/ray.tracer.object.defs.cc
  ${PROJECT_SOURCE_DIR}/src/ray.tracer.core.hpp
  ${PROJECT_SOURCE_DIR}/src/ray.tracer.core.cc
  ${PROJECT_SOURCE_DIR}/src/ray.tracer.material.handle.hpp
  ${PROJECT_SOURCE_DIR}/src/ray.tracer.material.defs.hpp
  ${PROJECT_SOURCE_DIR}/src/ray.tracer.material.defs.cc)

add_dependencies(${CMAKE_PROJECT_NAME} copy_data)

target_compile_features(${CMAKE_PROJECT_NAME} PRIVATE cxx_std_23)
target_include_directories(${CMAKE_PROJECT_NAME}
                           PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(
  ${CMAKE_PROJECT_NAME}
  PRIVATE fmt::fmt
          tl::expected
          tl::optional
          strong_type::strong_type
          # spdlog::spdlog
          nuklear
          quill::quill
          global-project-compile-options-lib
          SDL3::SDL3-static
          libzmq-static
          bfg::lyra
          ZppBits
          SDL3::SDL3-static
          glm::glm
          Vulkan::shaderc_combined
          max0x7ba::atomic_queue
          Yas
        )
