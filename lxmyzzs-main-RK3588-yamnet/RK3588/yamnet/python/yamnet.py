import numpy as np
from rknnlite.api import RKNNLite as RKNN
import argparse
import soundfile as sf
import scipy
import time

# 麦克风模式依赖 sounddevice，可选安装: pip install sounddevice
try:
    import sounddevice as sd
    HAS_SOUNDDEVICE = True
except ImportError:
    HAS_SOUNDDEVICE = False

SAMPLE_RATE = 16000
CHUNK_LENGTH = 3
MAX_N_SAMPLES = CHUNK_LENGTH * SAMPLE_RATE

# 紧急事件对应的 YAMNet 类别索引（仅在这些类别时判定为紧急）
# 玻璃破碎: Glass, Chink/clink, Shatter, Breaking
# 物体碰撞/砸击: Thump, Thunk, Bang, Slap, Whack, Smash/crash, Breaking
# 人的尖叫/哭叫: Shout, Yell, Screaming, Crying, Baby cry, Wail/moan
EMERGENCY_CLASS_INDICES = {
    6,   # Shout
    9,   # Yell
    11,  # Screaming
    19,  # Crying, sobbing
    20,  # Baby cry, infant cry
    22,  # Wail, moan
    435, # Glass
    436, # Chink, clink
    437, # Shatter
    454, # Thump, thud
    455, # Thunk
    460, # Bang
    461, # Slap, smack
    462, # Whack, thwack
    463, # Smash, crash
    464, # Breaking
}


def is_emergency_event(class_index):
    """判断 YAMNet 输出的类别索引是否为紧急事件（玻璃破碎/物体碰撞/尖叫哭叫）。"""
    return class_index in EMERGENCY_CLASS_INDICES


def ensure_sample_rate(waveform, original_sample_rate, desired_sample_rate=16000):
    if original_sample_rate != desired_sample_rate:
        print(f"resample_audio: {original_sample_rate} HZ -> {desired_sample_rate} HZ")
        desired_length = int(round(float(len(waveform)) / original_sample_rate * desired_sample_rate))
        waveform = scipy.signal.resample(waveform, desired_length)
    return waveform, desired_sample_rate


def ensure_channels(waveform, original_channels, desired_channels=1):
    if original_channels != desired_channels:
        print(f"convert_channels: {original_channels} -> {desired_channels}")
        waveform = np.mean(waveform, axis=1)
    return waveform, desired_channels


def init_model(model_path):
    model = RKNN()
    print('--> Loading model')
    ret = model.load_rknn(model_path)
    if ret != 0:
        print(f'Load RKNN model {model_path} failed!')
        exit(ret)
    print('done')

    print('--> Init runtime environment')
    ret = model.init_runtime()
    if ret != 0:
        print('Init runtime environment failed')
        exit(ret)
    print('done')
    return model


def run_model(model, audio):
    """audio: shape (1, N_SAMPLES) 的 float32 ndarray，内部会转为 list 以符合 RKNN 要求。"""
    return model.inference(inputs=[audio])


def release_model(model):
    model.release()


def post_process(outputs):
    scores = outputs[2]
    top_class_index = scores.mean(axis=0).argmax()
    return top_class_index


def pad_or_trim(array, length, axis=-1):
    if array.shape[axis] > length:
        array = array.take(indices=range(length), axis=axis)
    if array.shape[axis] < length:
        pad_widths = [(0, 0)] * array.ndim
        pad_widths[axis] = (0, length - array.shape[axis])
        array = np.pad(array, pad_widths)
    return array


def read_txt_to_dict(filename):
    data_dict = {}
    with open(filename, 'r', encoding='utf-8') as txtfile:
        for line in txtfile:
            line = line.strip().split(' ', 1)
            if len(line) < 2:
                continue
            key = line[0]
            value = line[1]
            data_dict[key] = value
    return data_dict


def test_mic(save_wav_path=None):
    """本地测试麦克风：录 3 秒，打印音量统计，可选保存为 WAV。不加载模型。"""
    if not HAS_SOUNDDEVICE:
        print("请先安装 sounddevice: pip install sounddevice")
        return
    dev = sd.query_devices(kind='input')
    print("默认输入设备:", dev)
    print("正在录制 3 秒 (16kHz 单声道)... 请对着麦克风说话或制造声音")
    rec = sd.rec(frames=MAX_N_SAMPLES, samplerate=SAMPLE_RATE, channels=1, dtype='float32')
    sd.wait()
    chunk = np.array(rec.flatten(), dtype=np.float32)
    mx = float(np.abs(chunk).max())
    rms = float(np.sqrt(np.mean(chunk ** 2)))
    nonzero = np.count_nonzero(np.abs(chunk) > 1e-6)
    print("-" * 50)
    print("音量统计（float32，范围约 -1~1）：")
    print(f"  最大绝对值: {mx:.6f}")
    print(f"  RMS:        {rms:.6f}")
    print(f"  非零采样数: {nonzero} / {len(chunk)}")
    if mx < 0.001 and rms < 0.0001:
        print("结论: 几乎没有信号，请检查：1) 麦克风是否被选中 2) 系统/应用是否静音 3) 是否选错设备")
    else:
        print("结论: 麦克风有信号，若推理仍多为 Silence，可能是环境较安静或模型对静音敏感。")
    if save_wav_path:
        sf.write(save_wav_path, chunk, SAMPLE_RATE)
        print(f"已保存: {save_wav_path}（可用播放器试听）")
    print("-" * 50)


def run_mic_loop(model, label):
    """麦克风实时检测：每 3 秒录一段，推理并打印结果，直到 Ctrl+C。"""
    if not HAS_SOUNDDEVICE:
        print("请先安装 sounddevice: pip install sounddevice")
        return
    print("麦克风实时检测已启动，每段 3 秒，按 Ctrl+C 退出")
    print("默认设备:", sd.query_devices(kind='input'))
    print("-" * 50)
    chunk_index = 0
    try:
        while True:
            # 录制 3 秒，16kHz 单声道 float32
            rec = sd.rec(frames=MAX_N_SAMPLES, samplerate=SAMPLE_RATE, channels=1, dtype='float32')
            sd.wait()
            chunk = np.array(rec.flatten(), dtype=np.float32)
            if len(chunk) < MAX_N_SAMPLES:
                chunk = np.pad(chunk, (0, MAX_N_SAMPLES - len(chunk)))
            input_data = np.expand_dims(chunk, 0)
            outputs = run_model(model, input_data)
            top_class_index = post_process(outputs)
            class_name = label.get(str(top_class_index), "Unknown")
            t0 = chunk_index * CHUNK_LENGTH
            t1 = t0 + CHUNK_LENGTH
            is_emergency = is_emergency_event(top_class_index)
            if is_emergency:
                print(f"[{t0:6.1f}s ~ {t1:6.1f}s]  声音类型 => {class_name}  ⚠️ 紧急事件")
            else:
                print(f"[{t0:6.1f}s ~ {t1:6.1f}s]  声音类型 => {class_name}")
            chunk_index += 1
    except KeyboardInterrupt:
        print("\n已停止麦克风检测")
    return


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Yamnet RK3588 音频检测')
    parser.add_argument('--model_path', type=str, help='.rknn 模型路径（test_mic 模式可不填）')
    parser.add_argument('--audio', type=str, default='../model/test.wav', help='测试音频路径（文件模式）')
    parser.add_argument('--mic', action='store_true', help='使用麦克风实时检测（每 3 秒一段）')
    parser.add_argument('--test_mic', action='store_true', help='仅测试麦克风是否正常：录 3 秒并打印音量统计')
    parser.add_argument('--save_mic', type=str, metavar='FILE', help='与 --test_mic 同用，将录制内容保存为 WAV 文件')
    parser.add_argument('--label_path', type=str, default='../model/yamnet_class_map.txt', help='类别映射文件')
    args = parser.parse_args()

    if args.test_mic:
        test_mic(save_wav_path=args.save_mic)
        exit(0)

    if not args.model_path:
        parser.error("请指定 --model_path，或使用 --test_mic 仅测试麦克风")
    label = read_txt_to_dict(args.label_path)
    model = init_model(args.model_path)

    if args.mic:
        run_mic_loop(model, label)
        release_model(model)
        exit(0)

    # 文件模式
    audio_path = args.audio
    audio_data, sample_rate = sf.read(audio_path)
    if audio_data.ndim == 2:
        audio_data, _ = ensure_channels(audio_data, 2)
    audio_data, _ = ensure_sample_rate(audio_data, sample_rate)
    audio_array = np.array(audio_data, dtype=np.float32).flatten()

    print(f"\n开始分析音频: {audio_path}")
    print("-" * 50)

    total_samples = len(audio_array)
    step = MAX_N_SAMPLES

    for start in range(0, total_samples, step):
        current_time_sec = start / SAMPLE_RATE
        chunk = audio_array[start: start + step]
        if len(chunk) < MAX_N_SAMPLES:
            chunk = np.pad(chunk, (0, MAX_N_SAMPLES - len(chunk)))
        input_data = np.expand_dims(chunk, 0)
        outputs = run_model(model, input_data)
        top_class_index = post_process(outputs)
        class_name = label.get(str(top_class_index), "Unknown")
        is_emergency = is_emergency_event(top_class_index)
        if is_emergency:
            print(f"[{current_time_sec:6.1f}s ~ {current_time_sec + CHUNK_LENGTH:6.1f}s]  声音类型 => {class_name}  ⚠️ 紧急事件")
        else:
            print(f"[{current_time_sec:6.1f}s ~ {current_time_sec + CHUNK_LENGTH:6.1f}s]  声音类型 => {class_name}")

    print("-" * 50)
    print("✅ 音频分析完成！")
    release_model(model)