cmake_minimum_required(VERSION 3.21)
cmake_policy(SET CMP0091 NEW)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()
project(camview VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()
set(_opencv_root "${CMAKE_CURRENT_SOURCE_DIR}/third_party/opencv")
if(WIN32)
    if(NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR "The bundled OpenCV binaries require 64-bit MSVC.")
    endif()
    if(CMAKE_CONFIGURATION_TYPES)
        set(CMAKE_CONFIGURATION_TYPES "Release;RelWithDebInfo;MinSizeRel" CACHE STRING "Available configurations" FORCE)
    elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
        message(FATAL_ERROR "The bundled OpenCV package only supports Release configurations.")
    endif()
    foreach(_file IN ITEMS include/opencv2/core.hpp lib/opencv_world460.lib
                           bin/opencv_world460.dll bin/opencv_videoio_msmf460_64.dll
                           bin/opencv_videoio_ffmpeg460_64.dll)
        if(NOT EXISTS "${_opencv_root}/${_file}")
            message(FATAL_ERROR "Missing bundled OpenCV file: ${_opencv_root}/${_file}")
        endif()
    endforeach()
    add_library(camview_opencv SHARED IMPORTED)
    set_target_properties(camview_opencv PROPERTIES
        IMPORTED_CONFIGURATIONS "RELEASE"
        IMPORTED_IMPLIB_RELEASE "${_opencv_root}/lib/opencv_world460.lib"
        IMPORTED_LOCATION_RELEASE "${_opencv_root}/bin/opencv_world460.dll"
        MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release
        MAP_IMPORTED_CONFIG_MINSIZEREL Release
        INTERFACE_INCLUDE_DIRECTORIES "${_opencv_root}/include")
    set(_opencv_target camview_opencv)
else()
    # Non-Windows platforms need their own dynamic OpenCV package.
    find_package(OpenCV 4.6 CONFIG REQUIRED COMPONENTS core imgproc imgcodecs videoio
        PATHS "${_opencv_root}" NO_DEFAULT_PATH)
    set(_opencv_target ${OpenCV_LIBS})
endif()
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
set(GLFW_LIBRARY_TYPE STATIC CACHE STRING "" FORCE)
if(MSVC)
    set(USE_MSVC_RUNTIME_LIBRARY_DLL ON CACHE BOOL "" FORCE)
endif()
if(UNIX AND NOT APPLE)
    # Detached ImGui platform windows require the X11 backend.
    set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
endif()
FetchContent_Declare(glfw
    URL https://codeload.github.com/glfw/glfw/zip/refs/tags/3.4
    URL_HASH SHA256=a133ddc3d3c66143eba9035621db8e0bcf34dba1ee9514a9e23e96afd39fd57a)
FetchContent_MakeAvailable(glfw)
set_property(DIRECTORY "${glfw_SOURCE_DIR}" PROPERTY EXCLUDE_FROM_ALL TRUE)
find_package(OpenGL REQUIRED)
set(_imgui_dir "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui")
add_library(camview_imgui STATIC
    ${_imgui_dir}/imgui.cpp
    ${_imgui_dir}/imgui_draw.cpp
    ${_imgui_dir}/imgui_tables.cpp
    ${_imgui_dir}/imgui_widgets.cpp
    ${_imgui_dir}/backends/imgui_impl_glfw.cpp
    ${_imgui_dir}/backends/imgui_impl_opengl3.cpp)
target_include_directories(camview_imgui PUBLIC ${_imgui_dir} ${_imgui_dir}/backends)
target_link_libraries(camview_imgui PUBLIC glfw OpenGL::GL)
find_package(Threads REQUIRED)
add_executable(camview WIN32 MACOSX_BUNDLE
    src/main.cpp src/camera.cpp src/platform.cpp)
target_link_libraries(camview PRIVATE camview_imgui
    ${_opencv_target} Threads::Threads)
if(NOT WIN32)
    target_include_directories(camview PRIVATE ${OpenCV_INCLUDE_DIRS})
endif()
if(WIN32)
    target_sources(camview PRIVATE src/media_foundation.cpp)
    target_compile_definitions(camview PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
    target_link_libraries(camview PRIVATE mfplat mfreadwrite mfuuid mf strmiids ole32 oleaut32)
endif()
if(MSVC)
    target_compile_options(camview PRIVATE /W4 /utf-8 /permissive-)
    target_compile_options(camview_imgui PRIVATE /utf-8)
    target_compile_options(glfw PRIVATE /utf-8)
else()
    target_compile_options(camview PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(APPLE)
    enable_language(OBJCXX)
    set_property(TARGET camview PROPERTY OBJCXX_STANDARD 17)
    set_property(TARGET camview PROPERTY OBJCXX_STANDARD_REQUIRED ON)
    target_sources(camview PRIVATE src/macos.mm)
    target_link_libraries(camview PRIVATE "-framework AVFoundation" "-framework CoreMedia")
    set_target_properties(camview PROPERTIES MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/src/Info.plist.in")
endif()
install(TARGETS camview RUNTIME DESTINATION . BUNDLE DESTINATION .)
if(WIN32)
    set(_opencv_msmf "${_opencv_root}/bin/opencv_videoio_msmf460_64.dll")
    set(_opencv_ffmpeg "${_opencv_root}/bin/opencv_videoio_ffmpeg460_64.dll")
    add_custom_command(TARGET camview POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:camview_opencv>" "$<TARGET_FILE_DIR:camview>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_opencv_msmf}" "$<TARGET_FILE_DIR:camview>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_opencv_ffmpeg}" "$<TARGET_FILE_DIR:camview>"
        VERBATIM)
    install(FILES "$<TARGET_FILE:camview_opencv>" "${_opencv_msmf}" "${_opencv_ffmpeg}" DESTINATION .)
    install(FILES "${_opencv_root}/LICENSE.txt" DESTINATION . RENAME opencv-LICENSE.txt)
    install(FILES "${_opencv_root}/LICENSE_FFMPEG.txt" DESTINATION . RENAME opencv-LICENSE_FFMPEG.txt)
    file(GLOB _opencv_licenses LIST_DIRECTORIES false "${_opencv_root}/licenses/*")
    foreach(_license IN LISTS _opencv_licenses)
        get_filename_component(_license_name "${_license}" NAME)
        install(FILES "${_license}" DESTINATION . RENAME "opencv-${_license_name}")
    endforeach()
endif()

install(FILES "${_imgui_dir}/LICENSE.txt" DESTINATION . RENAME imgui-LICENSE.txt)
install(FILES "${glfw_SOURCE_DIR}/LICENSE.md" DESTINATION . RENAME glfw-LICENSE.md)
