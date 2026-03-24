#!/bin/bash
# 在 cpp/build 目录运行 C++ yamnet demo，需先 cd 到 build
cd "$(dirname "$0")/build" || exit 1
export LD_LIBRARY_PATH="/home/orangepi/Desktop/lxmyzzs-main-RK3588-yamnet/3rdparty/rknpu2/rknpu2/Linux/librknn_api/aarch64${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
# 用法: ./run_demo.sh [模型.rknn] [音频.wav]，默认用 model 下的 yamnet 和 test.wav
RKNN="${1:-model/yamnet音频.rknn}"
WAV="${2:-model/test.wav}"
./rknn_yamnet_demo "$RKNN" "$WAV"
