#ifndef OSCILLOSCOPE_H
#define OSCILLOSCOPE_H

#include <vector>
#include <string>
#include <mutex>
#include <deque>
#include <chrono>
#include <atomic>
#include "implot.h"

class Oscilloscope {
public:
    Oscilloscope();
    ~Oscilloscope() = default;
    
    // 添加数据（线程安全）
    void AddData(int channel, float value);           // 添加单个通道数据
    void AddFrame(const std::vector<float>& frame);   // 添加一帧多通道数据
    
    // 绘制（在主线程调用）
    void Draw();

    
    // 配置
    void SetMaxPoints(int points);                   // 设置最大数据点数
    void SetChannelCount(int count);                 // 设置通道数
    void SetChannelName(int channel, const std::string& name);
    void SetChannelColor(int channel, const ImVec4& color);
    void SetChannelVisible(int channel, bool visible);
    
    // 控制
    void Clear();
    void SetPause(bool pause);
    bool IsPaused() const { return m_pausedAtomic.load(); }
    
    // 获取数据
    int GetChannelCount() const; 
    
private:
    // 通道配置
    struct ChannelInfo {
        std::string name;
        ImVec4 color;
        bool visible;
        ChannelInfo() : name(""), color(0,0,0,1), visible(true) {}
    };
    
    // 数据缓冲区结构
    struct DataBuffer {
        std::vector<std::deque<float>> channelsData;
        std::vector<ChannelInfo> channelsInfo;
        bool autoScale = true;
        float timeWindow = 5.0f;
        bool paused = false;
        int maxPoints = 1000;
        float updateRate = 0.0f;
        int totalPoints = 0;
        
        void Clear() {
            for (auto& channel : channelsData) {
                channel.clear();
            }
            totalPoints = 0;
        }
    };
    
    // 双重缓冲
    DataBuffer m_writeBuffer;  // 写入缓冲区（串口线程使用）
    DataBuffer m_readBuffer;   // 读取缓冲区（UI线程使用）
    mutable std::mutex m_bufferMutex;
    std::atomic<bool> m_buffersSwapped{false};
    
    // 原子暂停状态
    std::atomic<bool> m_pausedAtomic{false};
    
    // 统计
    std::chrono::steady_clock::time_point m_lastUpdate;
    
    // 内部方法
    void UpdateRateCounter();
    void EnsureChannelCount(DataBuffer& buffer, int count);
    void DrawControlPanel(const DataBuffer& buffer);
    void DrawStats(const DataBuffer& buffer);
    void DrawPolit(const DataBuffer& buffer);
    void SwapBuffers();
    
    // 默认颜色
    static ImVec4 GetDefaultColor(int index);
};

#endif // OSCILLOSCOPE_H