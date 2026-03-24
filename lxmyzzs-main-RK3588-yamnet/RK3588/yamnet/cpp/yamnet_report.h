#pragma once
#include <string>

namespace yamnet_report {

// 声音事件类型
enum class SoundEventType {
  Scream,      // 尖叫声
  GlassBreak,  // 玻璃破碎
  Fight,       // 打斗声
  Other        // 其他异常声音
};

struct Config {
  std::string api_base;   // http://127.0.0.1:8080/api
  std::string username;   // algo
  std::string password;   // algo123
  long camera_id = 1;     // 声音关联的 cameraId

  float min_conf   = 0.80f;   // 触发阈值
  int   cooldown_ms = 5000;   // 同一类型最小上报间隔(ms)
};

// 环境变量：CV_API_BASE / CV_USER / CV_PASS / CV_SOUND_CAMERA_ID / CV_SOUND_MIN_CONF / CV_SOUND_COOLDOWN_MS
Config load_config_from_env();

// 在 YAMNet 推理后调用
void maybe_report_sound_event(const Config& cfg,
                              SoundEventType top_event,
                              float conf,
                              const std::string& detail);

} // namespace yamnet_report