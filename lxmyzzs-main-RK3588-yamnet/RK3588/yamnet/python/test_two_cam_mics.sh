#!/usr/bin/env bash
# 分别用两个摄像头自带的麦克风录音，用于检查是否有声音
# 设备: card 2 = Web Camera, card 4 = Camera_1

set -e
RATE=16000
DUR=5
DIR="${1:-.}"

echo "===== 摄像头0 麦克风 (card 2: Web Camera) 录制 ${DUR} 秒 ====="
arecord -D plughw:2,0 -f S16_LE -r "$RATE" -c 1 -d "$DUR" -v "$DIR/cam0_mic.wav"
echo "已保存: $DIR/cam0_mic.wav"
echo ""

echo "===== 摄像头1 麦克风 (card 4: Camera_1) 录制 ${DUR} 秒 ====="
arecord -D plughw:4,0 -f S16_LE -r "$RATE" -c 1 -d "$DUR" -v "$DIR/cam1_mic.wav"
echo "已保存: $DIR/cam1_mic.wav"
echo ""

echo "试听: aplay $DIR/cam0_mic.wav  或  aplay $DIR/cam1_mic.wav"
