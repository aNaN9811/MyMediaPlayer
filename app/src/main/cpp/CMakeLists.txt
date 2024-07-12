cmake_minimum_required(VERSION 3.22.1)

project("mymediaplayer")

include_directories(
        libffmpeg/include
        librtmp
)

add_subdirectory(librtmp)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -L${CMAKE_SOURCE_DIR}/../jniLibs/${CMAKE_ANDROID_ARCH_ABI}")

# 批量导入
file(GLOB src_files *.cpp)

add_library(
        ${CMAKE_PROJECT_NAME}
        SHARED
        ${src_files}
)

target_link_libraries(
        ${CMAKE_PROJECT_NAME}
        android
        log
        z
        rtmp
        -Wl,--start-group
        avcodec avfilter avformat avutil swresample swscale
        -Wl,--end-group
        OpenSLES
)