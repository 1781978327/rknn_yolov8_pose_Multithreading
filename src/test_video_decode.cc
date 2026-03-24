// 测试视频解码 - 使用 CPU 软件解码
#include <opencv2/opencv.hpp>
#include <iostream>

using namespace cv;
using namespace std;

int main(int argc, char** argv) {
    if (argc < 2) {
        cout << "用法: " << argv[0] << " <video_file>" << endl;
        return -1;
    }
    
    string video_path = argv[1];
    
    cout << "正在打开视频: " << video_path << endl;
    
    // 强制使用 FFMPEG 后端（CPU 软件解码）
    VideoCapture cap(video_path, CAP_FFMPEG);
    
    if (!cap.isOpened()) {
        cout << "❌ 无法打开视频" << endl;
        return -1;
    }
    
    int width = cap.get(CAP_PROP_FRAME_WIDTH);
    int height = cap.get(CAP_PROP_FRAME_HEIGHT);
    double fps = cap.get(CAP_PROP_FPS);
    int frame_count = cap.get(CAP_PROP_FRAME_COUNT);
    
    cout << "✅ 视频打开成功" << endl;
    cout << "   分辨率: " << width << "x" << height << endl;
    cout << "   FPS: " << fps << endl;
    cout << "   总帧数: " << frame_count << endl;
    cout << "\n按 'q' 退出\n" << endl;
    
    namedWindow("Video Test", WINDOW_NORMAL);
    
    Mat frame;
    int current_frame = 0;
    
    while (true) {
        if (!cap.read(frame)) {
            cout << "视频结束或读取失败" << endl;
            break;
        }
        
        if (frame.empty()) {
            cout << "帧为空" << endl;
            continue;
        }
        
        current_frame++;
        
        char text[128];
        sprintf(text, "Frame: %d/%d | Size: %dx%d", 
                current_frame, frame_count, frame.cols, frame.rows);
        putText(frame, text, Point(10, 30), FONT_HERSHEY_SIMPLEX, 
                1.0, Scalar(0, 255, 0), 2);
        
        imshow("Video Test", frame);
        
        if (current_frame % 30 == 0) {
            cout << "已处理 " << current_frame << " 帧" << endl;
        }
        
        char key = waitKey(1);
        if (key == 'q' || key == 27) {
            break;
        }
    }
    
    cap.release();
    destroyAllWindows();
    
    cout << "\n总共处理了 " << current_frame << " 帧" << endl;
    return 0;
}
