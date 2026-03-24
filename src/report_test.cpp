#include <curl/curl.h>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <vector>
#include <fstream>

static std::string getenv_str(const char* k, const std::string& defv) {
    const char* v = std::getenv(k);
    return (v && *v) ? std::string(v) : defv;
}

static size_t write_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* s = static_cast<std::string*>(userdata);
    s->append(ptr, size * nmemb);
    return size * nmemb;
}

// 简单 base64 编码（用于把 JPEG 文件转成 data URL）
static const char kB64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::string base64_encode(const unsigned char* data, size_t len) {
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    for (size_t i = 0; i < len; i += 3) {
        uint32_t n = (uint32_t)data[i] << 16;
        if (i + 1 < len) n |= (uint32_t)data[i + 1] << 8;
        if (i + 2 < len) n |= (uint32_t)data[i + 2];

        out.push_back(kB64[(n >> 18) & 63]);
        out.push_back(kB64[(n >> 12) & 63]);
        out.push_back((i + 1 < len) ? kB64[(n >> 6) & 63] : '=');
        out.push_back((i + 2 < len) ? kB64[n & 63] : '=');
    }
    return out;
}

int main(int argc, char** argv) {
    std::string api_base = getenv_str("CV_API_BASE", "http://127.0.0.1:8080/api");
    std::string user     = getenv_str("CV_USER", "algo");
    std::string pass     = getenv_str("CV_PASS", "algo123");
    long camera_id       = std::strtol(getenv_str("CV_CAMERA_ID", "1").c_str(), nullptr, 10);

    int cls_id = 1;
    double conf = 0.88;
    if (argc >= 2) cls_id = std::atoi(argv[1]);
    if (argc >= 3) conf = std::atof(argv[2]);

    printf("API_BASE=%s USER=%s CAMERA_ID=%ld cls=%d conf=%.3f\n",
           api_base.c_str(), user.c_str(), camera_id, cls_id, conf);

    if (curl_global_init(CURL_GLOBAL_DEFAULT) != 0) {
        printf("curl_global_init 失败\n");
        return 1;
    }

    // 1. 登录拿 token
    std::string login_url = api_base + "/user/login";
    std::string login_body = "{\"username\":\"" + user + "\",\"password\":\"" + pass + "\"}";

    CURL* curl = curl_easy_init();
    if (!curl) {
        printf("curl_easy_init 失败\n");
        return 1;
    }

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    std::string resp;

    curl_easy_setopt(curl, CURLOPT_URL, login_url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, login_body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)login_body.size());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    printf("开始调用 /user/login ...\n");
    CURLcode res = curl_easy_perform(curl);
    long code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    printf("login 调用结束, res=%d http=%ld\n", (int)res, code);
    printf("login 响应: %s\n", resp.c_str());

    if (res != CURLE_OK || code != 200) {
        printf("登录失败，结束测试\n");
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        curl_global_cleanup();
        return 1;
    }

    // 非严格解析：简单截取 "token":"xxx"
    std::string token;
    {
        const std::string key = "\"token\":\"";
        size_t p = resp.find(key);
        if (p != std::string::npos) {
            p += key.size();
            size_t e = resp.find('"', p);
            if (e != std::string::npos) {
                token = resp.substr(p, e - p);
            }
        }
    }
    printf("解析到 token=%s\n", token.c_str());

    // 2. 从临时 JPEG 读取图像并转成 Base64
    const char* img_path = "/tmp/yolo_event.jpg";
    std::vector<unsigned char> img_data;
    {
        std::ifstream ifs(img_path, std::ios::binary);
        if (ifs) {
            ifs.seekg(0, std::ios::end);
            std::streamsize size = ifs.tellg();
            ifs.seekg(0, std::ios::beg);
            if (size > 0) {
                img_data.resize((size_t)size);
                ifs.read((char*)img_data.data(), size);
            }
        }
    }

    std::string img_b64;
    if (!img_data.empty()) {
        img_b64 = "data:image/jpeg;base64," +
                  base64_encode(img_data.data(), img_data.size());
    } else {
        img_b64 = "";
        printf("警告: 无法读取 %s, 本次上报不带图像\n", img_path);
    }

    // 3. 根据 cls_id 决定 type/desc
    const char* type = "person";
    const char* desc = "行人";
    switch (cls_id) {
        case 1: type = "fall";  desc = "摔倒"; break;
        case 2: type = "fight"; desc = "打架"; break;
        case 3: type = "knife"; desc = "持刀"; break;
        default: break;
    }

    // 4. 用 token 调一次 /detection/record
    std::string det_url = api_base + "/detection/record";
    char buf[256];
    std::snprintf(buf, sizeof(buf),
                  "{\\\"type\\\":\\\"%s\\\",\\\"desc\\\":\\\"%s\\\",\\\"confidence\\\":%.3f}",
                  type, desc, conf);
    std::string det_result_str = buf;

    std::string det_body =
        std::string("{") +
        "\"cameraId\":" + std::to_string(camera_id) + "," +
        "\"detectionTime\":\"\"," +
        "\"detectionResult\":\"" + det_result_str + "\"," +
        "\"imageBase64\":\"" + img_b64 + "\"" +
        "}";

    curl_slist_free_all(headers);
    headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    std::string auth = "Authorization: Bearer " + token;
    headers = curl_slist_append(headers, auth.c_str());

    resp.clear();
    curl_easy_setopt(curl, CURLOPT_URL, det_url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, det_body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)det_body.size());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);

    printf("开始调用 /detection/record ...\n");
    res = curl_easy_perform(curl);
    code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    printf("record 调用结束, res=%d http=%ld\n", (int)res, code);
    printf("record 响应: %s\n", resp.c_str());

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    curl_global_cleanup();
    return 0;
}

