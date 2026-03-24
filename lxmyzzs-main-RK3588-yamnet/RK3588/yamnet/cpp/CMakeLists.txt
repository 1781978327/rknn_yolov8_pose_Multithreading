cmake_minimum_required(VERSION 3.10)

project(rknn_yamnet_demo)

if (ENABLE_ASAN)
	message(STATUS "BUILD WITH ADDRESS SANITIZER")
	set (CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address")
	set (CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address")
	set (CMAKE_LINKER_FLAGS_DEBUG "${CMAKE_LINKER_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address")
endif ()

set(rknpu_yamnet_file rknpu2/yamnet.cc)
if(TARGET_SOC STREQUAL "rk1808" OR TARGET_SOC STREQUAL "rv1109" OR TARGET_SOC STREQUAL "rv1126")
    set(rknpu_yamnet_file rknpu1/yamnet.cc)
endif()

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../../3rdparty/ 3rdparty.out)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../../utils/ utils.out)

set(CMAKE_INSTALL_RPATH "$ORIGIN/lib")

file(GLOB SRCS ${CMAKE_CURRENT_SOURCE_DIR}/*.cc)

add_executable(${PROJECT_NAME}
    main.cc
    process.cc
    ${rknpu_yamnet_file}
    yamnet_report.cpp
)

# 3rdparty libsndfile.a 未用 -fPIC 编译，链接 PIE 可执行文件会失败，故禁用 PIE
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set_target_properties(${PROJECT_NAME} PROPERTIES LINK_FLAGS "-no-pie")
endif()

target_link_libraries(${PROJECT_NAME}
    fileutils
    audioutils   
    ${LIBRKNNRT}
    # 兼容：某些拷贝环境下 ${LIBRKNNRT} 为空，直接链接系统 librknnrt.so
    /lib/librknnrt.so
    dl
)

# 声音事件上报（Spring Boot API）
find_package(CURL REQUIRED)
target_link_libraries(${PROJECT_NAME} CURL::libcurl)

if (CMAKE_SYSTEM_NAME STREQUAL "Android")
    target_link_libraries(${PROJECT_NAME}
    log
)
endif()

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(THREADS_PREFER_PTHREAD_FLAG ON)
    find_package(Threads REQUIRED)
    target_link_libraries(${PROJECT_NAME} Threads::Threads)
endif()

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${LIBRKNNRT_INCLUDES}
    ${LIBTIMER_INCLUDES}
    # 兼容：有些环境下 3rdparty.cmake 未正确传出 LIBRKNNRT_INCLUDES
    # 直接把 rknn_api.h 的常见路径加进来，避免编译时报 “rknn_api.h: 没有那个文件或目录”
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../3rdparty/rknpu2/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../3rdparty/rknpu1/include
)

install(TARGETS ${PROJECT_NAME} DESTINATION .)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../model/test.wav DESTINATION ./model)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../model/yamnet_class_map.txt DESTINATION ./model)
file(GLOB RKNN_FILES "${CMAKE_CURRENT_SOURCE_DIR}/../model/*.rknn")
install(FILES ${RKNN_FILES} DESTINATION model)