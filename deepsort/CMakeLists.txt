cmake_minimum_required(VERSION 3.0.0)

# 与主工程一致，优先使用系统 OpenCV
# set(OpenCV_DIR /path/to/opencv/lib/cmake/opencv4)
find_package(OpenCV 4 REQUIRED)
include_directories(${OpenCV_INCLUDE_DIRS})

# Eigen3（DeepSORT 依赖），优先系统路径
if(NOT EIGEN3_INCLUDE_DIR)
  find_path(EIGEN3_INCLUDE_DIR NAMES Eigen/Core PATHS /usr/include/eigen3 /usr/local/include NO_DEFAULT_PATH)
endif()
if(NOT EIGEN3_INCLUDE_DIR)
  find_path(EIGEN3_INCLUDE_DIR NAMES Eigen/Core)
endif()

# RGA 路径（与主工程一致）
set(RGA_PATH ${PROJECT_SOURCE_DIR}/include/3rdparty/rga/RK3588)

set(
    include_libs  
    "${PROJECT_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/deepsort/include"
    ${PROJECT_SOURCE_DIR}/3rdparty/librknn_api/include
    ${PROJECT_SOURCE_DIR}/3rdparty/rga/include
    ${RGA_PATH}/include
    ${EIGEN3_INCLUDE_DIR}
)

# 使用系统 RKNN/RGA（与主工程一致）
find_library(RKNN_LIB rknnrt)
find_library(RGA_LIB rga)
set(
    dynamic_libs
    ${RKNN_LIB}
    ${RGA_LIB}
)

include_directories(${include_libs})
aux_source_directory(${PROJECT_SOURCE_DIR}/deepsort/src DEEPSORT_SRC_DIR)
# 从 src 目录添加源文件，但排除 report_test.cpp（需要 curl）
file(GLOB SRC_FILES "${PROJECT_SOURCE_DIR}/src/*.cpp" "${PROJECT_SOURCE_DIR}/src/*.cc")
list(FILTER SRC_FILES EXCLUDE REGEX ".*report_test\\.cpp$")
list(APPEND DEEPSORT_SRC_DIR ${SRC_FILES})

add_library(deepsort ${DEEPSORT_SRC_DIR})

target_link_libraries(deepsort ${OpenCV_LIBS})
target_link_libraries(deepsort ${dynamic_libs})