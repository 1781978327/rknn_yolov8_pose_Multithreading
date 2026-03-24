#include "yamnet_report.h"

#include <curl/curl.h>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace yamnet_report {
namespace {

std::string getenv_str(const char* k, const std::string& defv) {
  const char* v = std::getenv(k);
  return (v && *v) ? std::string(v) : defv;
}

long getenv_long(const char* k, long defv) {
  const char* v = std::getenv(k);
  if (!v || !*v) return defv;
  char* end = nullptr;
  long x = std::strtol(v, &end, 10);
  return (end && end != v) ? x : defv;
}

float getenv_float(const char* k, float defv) {
  const char* v = std::getenv(k);
  if (!v || !*v) return defv;
  char* end = nullptr;
  float x = std::strtof(v, &end);
  return (end && end != v) ? x : defv;
}

using Clock = std::chrono::steady_clock;
using Ms    = std::chrono::milliseconds;

struct State {
  std::mutex mu;
  std::string token;
  std::unordered_map<int, int64_t> last_report_ms_by_type;
  bool curl_inited = false;
};

State& st() {
  static State s;
  return s;
}

int64_t now_ms() {
  return std::chrono::duration_cast<Ms>(Clock::now().time_since_epoch()).count();
}

size_t write_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
  auto* s = static_cast<std::string*>(userdata);
  s->append(ptr, size * nmemb);
  return size * nmemb;
}

bool curl_global_init_once() {
  State& s = st();
  std::lock_guard<std::mutex> lk(s.mu);
  if (s.curl_inited) return true;
  if (curl_global_init(CURL_GLOBAL_DEFAULT) != 0) return false;
  s.curl_inited = true;
  return true;
}

bool http_post_json(const std::string& url,
                    const std::vector<std::string>& headers,
                    const std::string& body,
                    long* http_code_out,
                    std::string* resp_out) {
  if (!curl_global_init_once()) return false;

  CURL* curl = curl_easy_init();
  if (!curl) return false;

  struct curl_slist* hdrs = nullptr;
  for (const auto& h : headers) hdrs = curl_slist_append(hdrs, h.c_str());

  std::string resp;
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
  curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body.size());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

  CURLcode res = curl_easy_perform(curl);
  long code = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);

  curl_slist_free_all(hdrs);
  curl_easy_cleanup(curl);

  if (http_code_out) *http_code_out = code;
  if (resp_out) *resp_out = std::move(resp);
  return res == CURLE_OK;
}

std::string parse_token_from_login_resp(const std::string& resp) {
  const std::string key = "\"token\":\"";
  size_t p = resp.find(key);
  if (p == std::string::npos) return "";
  p += key.size();
  size_t e = resp.find('"', p);
  if (e == std::string::npos) return "";
  return resp.substr(p, e - p);
}

bool ensure_token(const Config& cfg) {
  State& s = st();
  std::lock_guard<std::mutex> lk(s.mu);
  if (!s.token.empty()) return true;

  std::string url  = cfg.api_base + "/user/login";
  std::string body = "{\"username\":\"" + cfg.username +
                     "\",\"password\":\"" + cfg.password + "\"}";
  std::vector<std::string> hdrs = {"Content-Type: application/json"};
  long code = 0;
  std::string resp;
  if (!http_post_json(url, hdrs, body, &code, &resp)) return false;
  if (code != 200) return false;
  std::string token = parse_token_from_login_resp(resp);
  if (token.empty()) return false;

  s.token = token;
  return true;
}

void type_to_text(SoundEventType t, const std::string& detail,
                  std::string& out_type, std::string& out_desc) {
  switch (t) {
    case SoundEventType::Scream:
      out_type = "sound_scream";
      out_desc = detail.empty() ? "尖叫声" : detail;
      break;
    case SoundEventType::GlassBreak:
      out_type = "sound_glass";
      out_desc = detail.empty() ? "玻璃破碎声" : detail;
      break;
    case SoundEventType::Fight:
      out_type = "sound_fight";
      out_desc = detail.empty() ? "打斗声" : detail;
      break;
    case SoundEventType::Other:
    default:
      out_type = "sound_other";
      out_desc = detail.empty() ? "其他异常声音" : detail;
      break;
  }
}

} // namespace

Config load_config_from_env() {
  Config cfg;
  cfg.api_base  = getenv_str("CV_API_BASE", "http://127.0.0.1:8080/api");
  cfg.username  = getenv_str("CV_USER", "algo");
  cfg.password  = getenv_str("CV_PASS", "algo123");
  cfg.camera_id = getenv_long("CV_SOUND_CAMERA_ID", 1);
  cfg.min_conf  = getenv_float("CV_SOUND_MIN_CONF", cfg.min_conf);
  cfg.cooldown_ms = (int)getenv_long("CV_SOUND_COOLDOWN_MS", cfg.cooldown_ms);
  return cfg;
}

void maybe_report_sound_event(const Config& cfg,
                              SoundEventType top_event,
                              float conf,
                              const std::string& detail) {
  if (conf < cfg.min_conf) return;

  int key = static_cast<int>(top_event);
  {
    State& s = st();
    std::lock_guard<std::mutex> lk(s.mu);
    int64_t now = now_ms();
    auto it = s.last_report_ms_by_type.find(key);
    if (it != s.last_report_ms_by_type.end() &&
        now - it->second < cfg.cooldown_ms) {
      return;
    }
    s.last_report_ms_by_type[key] = now;
  }

  if (!ensure_token(cfg)) {
    std::printf("[yamnet_report] ensure_token 失败, 跳过上报\n");
    return;
  }

  std::string ev_type, ev_desc;
  type_to_text(top_event, detail, ev_type, ev_desc);

  char buf[256];
  std::snprintf(buf, sizeof(buf),
                "{\\\"type\\\":\\\"%s\\\",\\\"desc\\\":\\\"%s\\\",\\\"confidence\\\":%.3f}",
                ev_type.c_str(), ev_desc.c_str(), conf);
  std::string det_result_str = buf;

  std::string body =
      std::string("{") +
      "\"cameraId\":" + std::to_string(cfg.camera_id) + "," +
      "\"detectionTime\":\"\"," +
      "\"detectionResult\":\"" + det_result_str + "\"," +
      "\"imageBase64\":\"\"" +
      "}";

  std::vector<std::string> hdrs = {
      "Content-Type: application/json",
      "Authorization: Bearer " + st().token,
  };
  long code = 0;
  std::string resp;
  std::string url = cfg.api_base + "/detection/record";
  std::printf("[yamnet_report] 上报声音事件: type=%s conf=%.3f\n",
              ev_type.c_str(), conf);
  if (!http_post_json(url, hdrs, body, &code, &resp) || code != 200) {
    std::printf("[yamnet_report] 上报失败 http=%ld resp=%s\n", code, resp.c_str());
  }
}

} // namespace yamnet_report