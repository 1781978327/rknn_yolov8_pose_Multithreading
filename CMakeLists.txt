cmake_minimum_required(VERSION 3.4.1)

project(rknn_yolov8_demo)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "-pthread")

# skip 3rd-party lib dependencies
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--allow-shlib-undefined")

# install target and libraries
set(CMAKE_INSTALL_PREFIX ${CMAKE_SOURCE_DIR}/install/rknn_yolov8_demo_${CMAKE_SYSTEM_NAME})

set(CMAKE_SKIP_INSTALL_RPATH FALSE)
set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib")

# rknn api
set(RKNN_API_PATH ${CMAKE_SOURCE_DIR}/../../runtime/RK3588/${CMAKE_SYSTEM_NAME}/librknn_api)
set(LIB_ARCH aarch64)
set(RKNN_RT_LIB ${CMAKE_SOURCE_DIR}/include/librknnrt.so)
include_directories(${RKNN_API_PATH}/include)
include_directories(${CMAKE_SOURCE_DIR}/include/3rdparty)

# opencv: 优先使用系统 OpenCV（解决 3rdparty 架构不匹配）
find_package(OpenCV QUIET)
if(OpenCV_FOUND)
  message(STATUS "Using system OpenCV: ${OpenCV_VERSION}")
  include_directories(${OpenCV_INCLUDE_DIRS})
  set(OpenCV_INSTALL_LIBS "")  # 系统库不复制
else()
  set(OpenCV_PATH ${CMAKE_SOURCE_DIR}/include/3rdparty/opencv)
  set(OpenCV_LIBS 
    ${OpenCV_PATH}/lib/libopencv_core.so
    ${OpenCV_PATH}/lib/libopencv_highgui.so
    ${OpenCV_PATH}/lib/libopencv_imgproc.so
    ${OpenCV_PATH}/lib/libopencv_videoio.so
    ${OpenCV_PATH}/lib/libopencv_imgcodecs.so
  )
  set(OpenCV_INSTALL_LIBS ${OpenCV_LIBS})
  include_directories(${OpenCV_PATH}/include/opencv4)
  message(STATUS "Using bundled OpenCV")
endif()

#rga
set(RGA_PATH ${CMAKE_SOURCE_DIR}/include/3rdparty/rga/RK3588)
set(LIB_ARCH aarch64)
set(RGA_LIB ${RGA_PATH}/lib/Linux/${LIB_ARCH}/librga.so)
include_directories( ${RGA_PATH}/include)
set(CMAKE_INSTALL_RPATH "lib")

# 可选：自编译带 rkmpp 的 FFmpeg，用于 YOLO 识别后 RTSP 硬件推流
# 用法: cmake -DFFMPEG_RKMPP_ROOT=/path/to/ffmpeg-rockchip/install ..
if(FFMPEG_RKMPP_ROOT)
  set(FFMPEG_RKMPP_INCLUDE "${FFMPEG_RKMPP_ROOT}/include")
  set(FFMPEG_RKMPP_LIB   "${FFMPEG_RKMPP_ROOT}/lib")
  if(NOT FFMPEG_RKMPP_RPATH)
    set(FFMPEG_RKMPP_RPATH "${FFMPEG_RKMPP_LIB}")
  endif()
  if(EXISTS "${FFMPEG_RKMPP_INCLUDE}/libavcodec/avcodec.h")
    add_compile_definitions(USE_RTSP_MPP)
    include_directories(${FFMPEG_RKMPP_INCLUDE})
    link_directories(${FFMPEG_RKMPP_LIB})
    set(RTSP_MPP_SRCS src/rtsp_mpp_sender.cpp)
    set(RTSP_MPP_LIBS -lavformat -lavcodec -lavutil -lswscale -lswresample -lavdevice -lz -lpthread -llzma -lm rockchip_mpp)
    # 关键：用 RPATH 让运行时能找到 libswresample.so.4 等 FFmpeg 依赖（尤其是间接依赖）
    set(RTSP_MPP_BUILD_RPATH "${FFMPEG_RKMPP_RPATH}")
    message(STATUS "RTSP+MPP: using FFmpeg at ${FFMPEG_RKMPP_ROOT}")
  else()
    message(STATUS "FFMPEG_RKMPP_ROOT 下未找到 avcodec.h，跳过 RTSP+MPP")
  endif()
endif()
if(NOT RTSP_MPP_SRCS)
  set(RTSP_MPP_SRCS "")
  set(RTSP_MPP_LIBS "")
  set(RTSP_MPP_BUILD_RPATH "")
endif()

# rknn_yolov8_demo
include_directories(${CMAKE_SOURCE_DIR}/include)

add_executable(rknn_yolov8_demo
  src/main.cc
  src/postprocess.cc
  src/rk_common.cc
  src/algo_reporter.cpp
  ${RTSP_MPP_SRCS}
)

target_link_libraries(rknn_yolov8_demo
  ${RKNN_RT_LIB}
  ${RGA_LIB}
  ${OpenCV_LIBS}
  ${RTSP_MPP_LIBS}
)

# 简单 HTTP 上报测试程序（不涉及 NPU / RTSP），用于验证 libcurl 自身是否会卡死
find_package(CURL QUIET)
if(CURL_FOUND)
  add_executable(report_test src/report_test.cpp)
  target_link_libraries(report_test CURL::libcurl)
else()
  message(STATUS "系统未找到 libcurl，report_test 将不会生成")
endif()

if(RTSP_MPP_BUILD_RPATH)
  # 用 BUILD_RPATH / INSTALL_RPATH，避免把 linker flag 当作“库”传入导致 rpath 不稳定
  set_target_properties(rknn_yolov8_demo PROPERTIES
    # 运行时优先从可执行文件同级的 lib/ 找依赖（最稳），再回退到 FFMPEG_RKMPP_ROOT/lib
    BUILD_RPATH "$ORIGIN/lib;${RTSP_MPP_BUILD_RPATH}"
    INSTALL_RPATH "$ORIGIN/lib;${RTSP_MPP_BUILD_RPATH}"
  )
  # 关键：关闭 new dtags，让 RPATH（而不是 RUNPATH）对间接依赖也生效
  target_link_options(rknn_yolov8_demo PRIVATE -Wl,--disable-new-dtags)

  # 把 FFmpeg 相关动态库复制到 build/lib，避免环境变量/系统库冲突导致运行时报错
  add_custom_command(TARGET rknn_yolov8_demo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:rknn_yolov8_demo>/lib"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${FFMPEG_RKMPP_LIB}/libavformat.so.60"
      "${FFMPEG_RKMPP_LIB}/libavcodec.so.60"
      "${FFMPEG_RKMPP_LIB}/libavutil.so.58"
      "${FFMPEG_RKMPP_LIB}/libswscale.so.7"
      "${FFMPEG_RKMPP_LIB}/libswresample.so.4"
      "$<TARGET_FILE_DIR:rknn_yolov8_demo>/lib"
    VERBATIM
  )
endif()


# install target and libraries
set(CMAKE_INSTALL_PREFIX ${CMAKE_SOURCE_DIR}/install/rknn_yolov8_demo_${CMAKE_SYSTEM_NAME})
install(TARGETS rknn_yolov8_demo DESTINATION ./)
install(PROGRAMS ${RKNN_RT_LIB} DESTINATION lib)
install(PROGRAMS ${RGA_LIB} DESTINATION lib)
if(OpenCV_INSTALL_LIBS)
  install(PROGRAMS ${OpenCV_INSTALL_LIBS} DESTINATION lib)
endif()
install(DIRECTORY model DESTINATION ./)
