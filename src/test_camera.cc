// 简单的摄像头测试程序
#include <opencv2/opencv.hpp>
#include <iostream>

using namespace cv;
using namespace std;

int main(int argc, char** argv) {
    if (argc < 2) {
        cout << "用法: " << argv[0] << " <camera_id>" << endl;
        cout << "例如: " << argv[0] << " 0" << endl;
        return -1;
    }
    
    int cam_id = atoi(argv[1]);
    
    cout << "正在打开摄像头 " << cam_id << "..." << endl;
    
    VideoCapture cap(cam_id, CAP_V4L2);
    
    if (!cap.isOpened()) {
        cout << "❌ 无法打开摄像头 " << cam_id << endl;
        return -1;
    }
    
    // 设置分辨率
    cap.set(CAP_PROP_FRAME_WIDTH, 640);
    cap.set(CAP_PROP_FRAME_HEIGHT, 480);
    
    int width = cap.get(CAP_PROP_FRAME_WIDTH);
    int height = cap.get(CAP_PROP_FRAME_HEIGHT);
    double fps = cap.get(CAP_PROP_FPS);
    
    cout << "✅ 摄像头打开成功" << endl;
    cout << "   分辨率: " << width << "x" << height << endl;
    cout << "   FPS: " << fps << endl;
    cout << "\n按 'q' 退出\n" << endl;
    
    namedWindow("Camera Test", WINDOW_NORMAL);
    
    Mat frame;
    int frame_count = 0;
    
    while (true) {
        if (!cap.read(frame)) {
            cout << "无法读取帧" << endl;
            break;
        }
        
        if (frame.empty()) {
            cout << "帧为空" << endl;
            continue;
        }
        
        frame_count++;
        
        // 显示帧信息
        char text[64];
        sprintf(text, "Frame: %d | Size: %dx%d", frame_count, frame.cols, frame.rows);
        putText(frame, text, Point(10, 30), FONT_HERSHEY_SIMPLEX, 1.0, Scalar(0, 255, 0), 2);
        
        // 在中心画一个圆，确认画面不是灰色
        circle(frame, Point(width/2, height/2), 50, Scalar(0, 0, 255), 3);
        
        imshow("Camera Test", frame);
        
        if (frame_count % 30 == 0) {
            cout << "Frame " << frame_count << " | 画面通道: " << frame.channels() 
                 << " | 类型: " << frame.type() << endl;
        }
        
        char key = waitKey(1);
        if (key == 'q' || key == 27) {
            break;
        }
    }
    
    cap.release();
    destroyAllWindows();
    
    cout << "\n总共处理了 " << frame_count << " 帧" << endl;
    return 0;
}
