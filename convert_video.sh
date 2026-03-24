#!/bin/bash
# 视频转码脚本 - 转换为 RK3588 兼容格式

if [ $# -lt 2 ]; then
    echo "用法: $0 <输入视频> <输出视频>"
    echo "例如: $0 person.mp4 person_converted.mp4"
    exit 1
fi

INPUT="$1"
OUTPUT="$2"

echo "正在转码: $INPUT -> $OUTPUT"
echo "使用 RK3588 兼容的 H.264 编码参数..."

ffmpeg -i "$INPUT" \
    -c:v h264_rkmpp \
    -pix_fmt yuv420p \
    -b:v 2M \
    -c:a aac \
    -b:a 128k \
    -y \
    "$OUTPUT"

if [ $? -eq 0 ]; then
    echo "✅ 转码成功: $OUTPUT"
    echo ""
    echo "现在可以运行:"
    echo "./rknn_yolov8_with_deepsort ../model/yolov8_pose.rknn $OUTPUT"
else
    echo "❌ 转码失败"
    exit 1
fi
