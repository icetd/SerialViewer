#include "Oscilloscope.h"
#include <algorithm>
#include <numeric>
#include <imgui.h>
#include <implot.h>
#include <cstring>
#include <vector>
#include <iostream>

template <typename T>
T clamp_value(T value, T min_val, T max_val)
{
    return (value < min_val) ? min_val : ((value > max_val) ? max_val : value);
}

Oscilloscope::Oscilloscope()
{
    m_lastUpdate = std::chrono::steady_clock::now();

    // 初始化写入缓冲区
    m_writeBuffer.maxPoints = 1000;
    m_writeBuffer.autoScale = true;
    m_writeBuffer.timeWindow = 5.0f;
    m_writeBuffer.paused = false;

    m_readBuffer = m_writeBuffer;

    // 初始化原子状态
    m_pausedAtomic = false;

    // 默认创建4个通道
    SetChannelCount(0);
}

void Oscilloscope::AddData(int channel, float value)
{
    // 使用原子变量快速检查暂停状态
    if (m_pausedAtomic.load() || channel < 0) return;

    std::lock_guard<std::mutex> lock(m_bufferMutex);

    // 再次检查暂停状态（加锁后）
    if (m_writeBuffer.paused) return;

    EnsureChannelCount(m_writeBuffer, channel + 1);

    m_writeBuffer.channelsData[channel].push_back(value);

    // 限制数据长度
    if (m_writeBuffer.channelsData[channel].size() > static_cast<size_t>(m_writeBuffer.maxPoints)) {
        m_writeBuffer.channelsData[channel].pop_front();
    }

    UpdateRateCounter();
    m_buffersSwapped = false;
}

void Oscilloscope::AddFrame(const std::vector<float> &frame)
{
    // 使用原子变量快速检查暂停状态
    if (m_pausedAtomic.load() || frame.empty()) return;

    std::lock_guard<std::mutex> lock(m_bufferMutex);

    // 再次检查暂停状态（加锁后）
    if (m_writeBuffer.paused) return;

    EnsureChannelCount(m_writeBuffer, static_cast<int>(frame.size()));

    // 添加数据到每个通道
    for (size_t i = 0; i < frame.size(); ++i) {
        m_writeBuffer.channelsData[i].push_back(frame[i]);

        // 限制数据长度
        if (m_writeBuffer.channelsData[i].size() > static_cast<size_t>(m_writeBuffer.maxPoints)) {
            m_writeBuffer.channelsData[i].pop_front();
        }
    }

    UpdateRateCounter();
    m_buffersSwapped = false;
}

void Oscilloscope::Draw()
{
    // 交换缓冲区（只在绘制时交换）
    SwapBuffers();
    // 现在使用读取缓冲区（不需要锁）
    const DataBuffer &buffer = m_readBuffer;


    DrawControlPanel(buffer);
    DrawStats(buffer);
    DrawPolit(buffer);
}

void Oscilloscope::SwapBuffers()
{
    // 只有在新数据到达时才交换
    if (!m_buffersSwapped) {
        std::lock_guard<std::mutex> lock(m_bufferMutex);

        // 交换通道数据
        m_readBuffer.channelsData = m_writeBuffer.channelsData;

        // 复制配置
        m_readBuffer.channelsInfo = m_writeBuffer.channelsInfo;
        m_readBuffer.autoScale = m_writeBuffer.autoScale;
        m_readBuffer.timeWindow = m_writeBuffer.timeWindow;
        m_readBuffer.paused = m_writeBuffer.paused;
        m_readBuffer.maxPoints = m_writeBuffer.maxPoints;
        m_readBuffer.updateRate = m_writeBuffer.updateRate;

        // 计算总数据点
        m_readBuffer.totalPoints = 0;
        for (const auto &channel_data : m_readBuffer.channelsData) {
            m_readBuffer.totalPoints += static_cast<int>(channel_data.size());
        }

        m_buffersSwapped = true;
    }
}

void Oscilloscope::SetMaxPoints(int points)
{
    std::lock_guard<std::mutex> lock(m_bufferMutex);
    m_writeBuffer.maxPoints = std::max(1, points);
    m_readBuffer.maxPoints = m_writeBuffer.maxPoints; // 同步到读取缓冲区

    // 裁剪现有数据
    for (auto &channel_data : m_writeBuffer.channelsData) {
        if (channel_data.size() > static_cast<size_t>(m_writeBuffer.maxPoints)) {
            int remove_count = static_cast<int>(channel_data.size()) - m_writeBuffer.maxPoints;
            for (int i = 0; i < remove_count; ++i) {
                channel_data.pop_front();
            }
        }
    }
}

void Oscilloscope::SetChannelCount(int count)
{
    std::lock_guard<std::mutex> lock(m_bufferMutex);

    if (count <= 0) count = 1;

    m_writeBuffer.channelsData.resize(count);
    m_writeBuffer.channelsInfo.resize(count);

    // 初始化新通道
    for (int i = 0; i < count; ++i) {
        if (m_writeBuffer.channelsInfo[i].name.empty()) {
            m_writeBuffer.channelsInfo[i].name = u8"通道" + std::to_string(i + 1);
        }
        if (m_writeBuffer.channelsInfo[i].color.x == 0 && m_writeBuffer.channelsInfo[i].color.y == 0 && m_writeBuffer.channelsInfo[i].color.z == 0) {
            m_writeBuffer.channelsInfo[i].color = GetDefaultColor(i);
        }
        m_writeBuffer.channelsInfo[i].visible = true;
    }

    // 同步到读取缓冲区
    m_readBuffer.channelsInfo = m_writeBuffer.channelsInfo;
}

void Oscilloscope::SetChannelName(int channel, const std::string &name)
{
    std::lock_guard<std::mutex> lock(m_bufferMutex);
    if (channel >= 0 && channel < static_cast<int>(m_writeBuffer.channelsInfo.size())) {
        m_writeBuffer.channelsInfo[channel].name = name;
        m_readBuffer.channelsInfo[channel].name = name; // 同步到读取缓冲区
    }
}

void Oscilloscope::SetChannelColor(int channel, const ImVec4 &color)
{
    std::lock_guard<std::mutex> lock(m_bufferMutex);
    if (channel >= 0 && channel < static_cast<int>(m_writeBuffer.channelsInfo.size())) {
        m_writeBuffer.channelsInfo[channel].color = color;
        m_readBuffer.channelsInfo[channel].color = color; // 同步到读取缓冲区
    }
}

void Oscilloscope::SetChannelVisible(int channel, bool visible)
{
    std::lock_guard<std::mutex> lock(m_bufferMutex);
    if (channel >= 0 && channel < static_cast<int>(m_writeBuffer.channelsInfo.size())) {
        m_writeBuffer.channelsInfo[channel].visible = visible;
        m_readBuffer.channelsInfo[channel].visible = visible; // 同步到读取缓冲区
    }
}

void Oscilloscope::Clear()
{
    std::lock_guard<std::mutex> lock(m_bufferMutex);
    m_writeBuffer.Clear();
    m_readBuffer.Clear(); // 同时清除读取缓冲区
}

void Oscilloscope::SetPause(bool pause)
{
    // 更新原子状态
    m_pausedAtomic.store(pause);

    // 更新缓冲区状态
    std::lock_guard<std::mutex> lock(m_bufferMutex);
    m_writeBuffer.paused = pause;
    m_readBuffer.paused = pause; // 同步到读取缓冲区

    std::cout << "示波器暂停状态设置为: " << (pause ? "暂停" : "运行") << std::endl;
}

int Oscilloscope::GetChannelCount() const
{
    std::lock_guard<std::mutex> lock(m_bufferMutex);
    return static_cast<int>(m_writeBuffer.channelsData.size());
}

void Oscilloscope::UpdateRateCounter()
{
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration<float>(now - m_lastUpdate).count();

    if (elapsed > 0.5f) { // 每0.5秒更新一次
        m_writeBuffer.updateRate = 1.0f / elapsed;
        m_lastUpdate = now;
    }
}

void Oscilloscope::EnsureChannelCount(DataBuffer &buffer, int count)
{
    if (count > static_cast<int>(buffer.channelsData.size())) {
        int old_size = static_cast<int>(buffer.channelsData.size());
        buffer.channelsData.resize(count);
        buffer.channelsInfo.resize(count);

        // 初始化新通道
        for (int i = old_size; i < count; ++i) {
            if (buffer.channelsInfo[i].name.empty()) {
                buffer.channelsInfo[i].name = u8"通道" + std::to_string(i + 1);
            }
            if (buffer.channelsInfo[i].color.x == 0 && buffer.channelsInfo[i].color.y == 0 && buffer.channelsInfo[i].color.z == 0) {
                buffer.channelsInfo[i].color = GetDefaultColor(i);
            }
            buffer.channelsInfo[i].visible = true;
        }
    }
}

void Oscilloscope::DrawControlPanel(const DataBuffer &buffer)
{
    ImGui::Begin(u8"参数控制");

    ImGui::PushItemWidth(-ImGui::GetWindowWidth() * 0.2f);
    ImGui::AlignTextToFramePadding();
    
    ImGui::SeparatorText(u8"状态设置");
    bool is_paused = m_pausedAtomic.load();
    if (ImGui::Button(is_paused ? u8"继续" : u8"暂停", ImVec2(80, 30))) {
        std::cout << "点击暂停按钮，当前状态: " << is_paused << std::endl;
        SetPause(!is_paused);
    }
    ImGui::SameLine();
    if (ImGui::Button(u8"清除", ImVec2(80, 30))) {
        Clear();
        std::cout << "清除所有数据" << std::endl;
    }
    ImGui::SameLine();
    if (is_paused) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), u8"[已暂停]");
    } else {
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), u8"[运行中]");
    }
    ImGui::SameLine();
    ImGui::Text(u8"通道: %d  总点数: %d", (int)buffer.channelsData.size(), buffer.totalPoints);


    ImGui::SeparatorText(u8"时间窗口设置");
    // 时间窗口滑块
    float timeWindow = buffer.timeWindow;
    ImGui::SetNextItemWidth(250);
    if (ImGui::SliderFloat(u8"显示时间 (秒)", &timeWindow, 0.01f, 60.0f, "%.2f s")) {
        std::lock_guard<std::mutex> lock(m_bufferMutex);
        m_writeBuffer.timeWindow = timeWindow;
        m_readBuffer.timeWindow = timeWindow;
        std::cout << "时间窗口设置为: " << timeWindow << " 秒" << std::endl;
    }
    // 时间窗口快捷按钮
    float quick_times[] = {1.0f, 2.0f, 5.0f, 10.0f, 30.0f};
    for (float t : quick_times) {
        if (ImGui::SmallButton((std::to_string((int)t) + u8"秒").c_str())) {
            std::lock_guard<std::mutex> lock(m_bufferMutex);
            m_writeBuffer.timeWindow = t;
            m_readBuffer.timeWindow = t;
            std::cout << "时间窗口快捷设置为: " << t << " 秒" << std::endl;
        }
        ImGui::SameLine();
    }
    ImGui::NewLine();
    ImGui::SetNextItemWidth(100);
    static float manual_time_input = timeWindow;
    if (ImGui::InputFloat(u8"手动输入(秒)", &manual_time_input, 0.01f, 1.0f, "%.2f")) {
        if (manual_time_input >= 0.1f && manual_time_input <= 60.0f) {
            std::lock_guard<std::mutex> lock(m_bufferMutex);
            m_writeBuffer.timeWindow = manual_time_input;
            m_readBuffer.timeWindow = manual_time_input;
            std::cout << "时间窗口手动设置为: " << manual_time_input << " 秒" << std::endl;
        }
    }
    ImGui::Text(u8"当前时间窗口: %.1f 秒", timeWindow);
    ImGui::Spacing();

    ImGui::SeparatorText(u8"采样点数设置");
    int maxPoints = buffer.maxPoints;
    ImGui::SetNextItemWidth(250);
    if (ImGui::SliderInt(u8"最大采样点数", &maxPoints, 1, 10000, "%d points")) {
        SetMaxPoints(maxPoints);
        std::cout << "最大点数设置为: " << maxPoints << " 点" << std::endl;
    }

    int quick_points[] = {500, 1000, 2000, 5000, 10000};
    for (int p : quick_points) {
        if (ImGui::SmallButton((std::to_string(p) + u8"点").c_str())) {
            SetMaxPoints(p);
            std::cout << "最大点数快捷设置为: " << p << " 点" << std::endl;
        }
        ImGui::SameLine();
    }
    ImGui::NewLine();
    ImGui::SetNextItemWidth(100);
    static int manual_points_input = maxPoints;
    if (ImGui::InputInt(u8"手动输入(点)", &manual_points_input, 100, 1000)) {
        if (manual_points_input >= 100 && manual_points_input <= 10000) {
            SetMaxPoints(manual_points_input);
            std::cout << "采样点数手动设置为: " << manual_points_input << " 点" << std::endl;
        }
    }
    ImGui::Text(u8"当前最大点数: %d 点", maxPoints);
    ImGui::Spacing();
 
    // 采样率显示
    ImGui::Text(u8"buffer更新率: %.1f Hz", buffer.updateRate);

    ImGui::SameLine();

    // 估算采样率
    if (timeWindow > 0 && maxPoints > 0) {
        float estimated_rate = maxPoints / timeWindow;
        ImGui::Text(u8"估算采样率: %.0f Hz", estimated_rate);
    }

    ImGui::SeparatorText(u8"高级设置:");
    // 自动缩放开关
    bool autoScale = buffer.autoScale;
    if (ImGui::Checkbox(u8"自动缩放 Y 轴", &autoScale)) {
        std::lock_guard<std::mutex> lock(m_bufferMutex);
        m_writeBuffer.autoScale = autoScale;
        m_readBuffer.autoScale = autoScale;
        std::cout << "自动缩放: " << (autoScale ? "开启" : "关闭") << std::endl;
    }


    ImGui::Spacing();
    ImGui::SeparatorText(u8"通道控制");
    ImGui::Text(u8"当前通道数: %d", (int)buffer.channelsData.size());
    ImGui::Spacing();

    // 通道详细信息表格
    if (ImGui::BeginTable("ChannelsTable", 5,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
        // 表头
        ImGui::TableSetupColumn(u8"显示", ImGuiTableColumnFlags_WidthFixed, 40);
        ImGui::TableSetupColumn(u8"颜色", ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableSetupColumn(u8"通道名", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn(u8"数据点数", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn(u8"最新值", ImGuiTableColumnFlags_WidthFixed, 120);
        ImGui::TableHeadersRow();

        // 通道行
        static std::vector<float> last_values(buffer.channelsData.size(), 0.0f);
        if (last_values.size() != buffer.channelsData.size()) {
            last_values.resize(buffer.channelsData.size(), 0.0f);
        }

        for (int i = 0; i < static_cast<int>(buffer.channelsInfo.size()); ++i) {
            ImGui::PushID(i);
            ImGui::TableNextRow();

            // 第一列：显示/隐藏
            ImGui::TableSetColumnIndex(0);
            bool visible = buffer.channelsInfo[i].visible;
            if (ImGui::Checkbox("##Visible", &visible)) {
                SetChannelVisible(i, visible);
            }

            // 第二列：颜色
            ImGui::TableSetColumnIndex(1);
            ImVec4 color = buffer.channelsInfo[i].color;
            if (ImGui::ColorEdit4("##Color", (float *)&color,
                                  ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoSidePreview)) {
                SetChannelColor(i, color);
            }

            // 第三列：通道名
            ImGui::TableSetColumnIndex(2);
            char name_buf[32];
            strncpy(name_buf, buffer.channelsInfo[i].name.c_str(), sizeof(name_buf));
            name_buf[sizeof(name_buf) - 1] = '\0';
            if (ImGui::InputText("##Name", name_buf, sizeof(name_buf))) {
                SetChannelName(i, name_buf);
            }

            // 第四列：数据点数
            ImGui::TableSetColumnIndex(3);
            int point_count = buffer.channelsData[i].size();
            ImGui::Text("%d", point_count);

            // 第五列：最新值
            ImGui::TableSetColumnIndex(4);
            if (!buffer.channelsData[i].empty()) {
                float latest_value = buffer.channelsData[i].back();
                ImGui::Text("%.6f", latest_value);
            } else {
                ImGui::TextDisabled("--");
            }

            ImGui::PopID();
        }

        ImGui::EndTable();
    }

    ImGui::PopItemWidth();
    ImGui::End();
}

void Oscilloscope::DrawStats(const DataBuffer &buffer)
{
    ImGui::Begin(u8"状态");
    ImGui::Separator();

    // 统计信息显示
    ImGui::SeparatorText(u8"统计信息");

    // 使用简单表格显示统计
    if (ImGui::BeginTable("StatsTable", 4,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        // 表头
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Text(u8"通道");
        ImGui::TableSetColumnIndex(1);
        ImGui::Text(u8"最小值");
        ImGui::TableSetColumnIndex(2);
        ImGui::Text(u8"最大值");
        ImGui::TableSetColumnIndex(3);
        ImGui::Text(u8"平均值");

        // 通道统计信息
        for (size_t i = 0; i < buffer.channelsData.size(); ++i) {
            if (buffer.channelsData[i].size() < 2) continue;

            ImGui::TableNextRow();

            // 通道名
            ImGui::TableSetColumnIndex(0);
            ImGui::TextColored(buffer.channelsInfo[i].color, "%s",
                               buffer.channelsInfo[i].name.c_str());

            // 计算统计值
            auto minmax = std::minmax_element(buffer.channelsData[i].begin(),
                                              buffer.channelsData[i].end());
            float sum = std::accumulate(buffer.channelsData[i].begin(),
                                        buffer.channelsData[i].end(), 0.0f);
            float avg = sum / buffer.channelsData[i].size();

            // 最小值
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%.4f", *minmax.first);

            // 最大值
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%.4f", *minmax.second);

            // 平均值
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%.4f", avg);
        }

        // 全局统计行
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Text(u8"全局统计");
        ImGui::TableSetColumnIndex(1);
        ImGui::Text(u8"更新率: %.1f Hz", buffer.updateRate);
        ImGui::TableSetColumnIndex(2);
        ImGui::Text(u8"总点数: %d", buffer.totalPoints);
        ImGui::TableSetColumnIndex(3);
        if (buffer.timeWindow > 0) {
            float points_per_second = buffer.totalPoints / buffer.timeWindow;
            ImGui::Text(u8"点/秒: %.0f", points_per_second);
        }

        ImGui::EndTable();
    }

    ImGui::End();
}

void Oscilloscope::DrawPolit(const DataBuffer &buffer)
{
    ImGui::Begin(u8"图像");
    // 绘制图表
    if (ImPlot::BeginPlot("##Oscilloscope", ImVec2(-1, -1))) {
        ImGuiIO &io = ImGui::GetIO();
        if (ImGui::IsItemHovered() && io.MouseWheel != 0.0f) {
            float wheel = io.MouseWheel;

            float speed = 0.15f;
            if (io.KeyCtrl) speed = 0.03f;
            if (io.KeyShift) speed = 0.3f;

            float zoomFactor = 1.0f - wheel * speed;
            float newTimeWindow = buffer.timeWindow * zoomFactor;
            newTimeWindow = clamp_value(newTimeWindow, 0.01f, 60.0f);

            std::lock_guard<std::mutex> lock(m_bufferMutex);
            m_writeBuffer.timeWindow = newTimeWindow;
            m_readBuffer.timeWindow = newTimeWindow;
        }

        // 设置坐标轴
        ImPlot::SetupAxes(u8"时间", u8"幅值",
                          ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_RangeFit,
                          ImPlotAxisFlags_None);

        // 静态变量存储波形平移偏移
        static float waveform_offset = 0.0f;
        static bool is_dragging = false;
        
        // 处理暂停状态下的波形平移
        if (buffer.paused) {
            // 检查鼠标拖拽
            if (ImPlot::IsPlotHovered()) {
                if (ImGui::IsMouseClicked(0)) {
                    is_dragging = true;
                }
                
                if (ImGui::IsMouseReleased(0)) {
                    is_dragging = false;
                }
                
                if (is_dragging && ImGui::IsMouseDragging(0)) {
                    // 获取鼠标拖拽距离
                    ImVec2 drag_delta = ImGui::GetMouseDragDelta(0);
                    
                    // 计算波形平移量
                    // 每个像素对应数据点的移动量
                    float drag_sensitivity = 0.5f; // 调整这个值改变灵敏度
                    waveform_offset += drag_delta.x * drag_sensitivity;
                    
                    // 限制平移范围：不能超出数据范围
                    if (!buffer.channelsData.empty() && !buffer.channelsData[0].empty()) {
                        int max_data_points = static_cast<int>(buffer.channelsData[0].size());
                        int max_offset = std::max(0, max_data_points - 1);
                        waveform_offset = clamp_value(waveform_offset, -max_offset * 0.5f, max_offset * 0.5f);
                    }
                    
                    // 重置拖拽状态
                    ImGui::ResetMouseDragDelta(0);
                }
            }
            
            // 显示平移提示
            if (is_dragging) {
                ImGui::SetTooltip(u8"平移: %.1f 点", waveform_offset);
            }
            
            // 重置按钮
            if (ImGui::IsKeyPressed(ImGuiKey_R)) {
                waveform_offset = 0.0f;
            }
        } else {
            // 运行状态时重置平移
            waveform_offset = 0.0f;
            is_dragging = false;
        }

        // 设置时间轴范围
        ImPlot::SetupAxisLimits(ImAxis_X1, 0, buffer.timeWindow, ImGuiCond_Always);

        // 计算Y轴范围（如果需要自动缩放）
        if (buffer.autoScale) {
            float y_min = FLT_MAX;
            float y_max = -FLT_MAX;

            for (int ch = 0; ch < static_cast<int>(buffer.channelsData.size()); ++ch) {
                if (!buffer.channelsInfo[ch].visible || buffer.channelsData[ch].empty()) continue;

                auto minmax = std::minmax_element(buffer.channelsData[ch].begin(),
                                                  buffer.channelsData[ch].end());
                y_min = std::min(y_min, *minmax.first);
                y_max = std::max(y_max, *minmax.second);
            }

            if (y_max > y_min) {
                float padding = (y_max - y_min) * 0.1f;
                ImPlot::SetupAxisLimits(ImAxis_Y1, y_min - padding, y_max + padding, ImGuiCond_Always);
            }
        }

        // 绘制每个通道
        for (int ch = 0; ch < static_cast<int>(buffer.channelsData.size()); ++ch) {
            if (!buffer.channelsInfo[ch].visible || buffer.channelsData[ch].empty()) continue;

            const auto& data = buffer.channelsData[ch];
            
            // 创建时间数据，考虑波形平移
            std::vector<float> plot_data;
            std::vector<float> time_data;
            
            float time_step = 1.0f / 100.0f; // 假设100Hz采样率
            
            // 计算可见范围的数据点
            int start_idx = 0;
            int end_idx = static_cast<int>(data.size());
            
            // 如果平移了波形，调整显示的数据范围
            if (waveform_offset != 0.0f && buffer.paused) {
                // 计算基于平移的偏移索引
                int offset_idx = static_cast<int>(waveform_offset);
                
                // 确保索引在有效范围内
                start_idx = std::max(0, offset_idx);
                end_idx = std::min(static_cast<int>(data.size()), 
                                  static_cast<int>(data.size()) + offset_idx);
                
                // 如果偏移是负数（向左移动），需要调整开始和结束位置
                if (offset_idx < 0) {
                    start_idx = 0;
                    end_idx = std::min(static_cast<int>(data.size()), 
                                      static_cast<int>(data.size()) + offset_idx);
                }
            }
            
            // 准备可见数据
            for (int i = start_idx; i < end_idx; ++i) {
                if (i >= 0 && i < static_cast<int>(data.size())) {
                    plot_data.push_back(data[i]);
                    // 时间从0开始，但显示的数据是平移后的
                    time_data.push_back(static_cast<float>(i - start_idx) * time_step);
                }
            }
            
            if (plot_data.empty()) continue;

            // 设置线条样式
            ImPlot::SetNextLineStyle(buffer.channelsInfo[ch].color, 1.5f);

            // 绘制
            ImPlot::PlotLine(buffer.channelsInfo[ch].name.c_str(),
                             time_data.data(),
                             plot_data.data(),
                             static_cast<int>(plot_data.size()));
        }
        
        // 在图表上显示控制提示
        if (buffer.paused) {
            ImPlot::Annotation(buffer.timeWindow * 0.9f, 0, 
                              ImVec4(1, 1, 1, 0.5f), ImVec2(0, 0), true,
                              u8"暂停模式 - 拖拽平移波形 | R键重置");
        }

        ImPlot::EndPlot();
    }

    ImGui::End();
}

ImVec4 Oscilloscope::GetDefaultColor(int index)
{
    static const ImVec4 colors[] = {
        ImVec4(0.0f, 1.0f, 0.0f, 1.0f), // 绿
        ImVec4(1.0f, 0.0f, 0.0f, 1.0f), // 红
        ImVec4(0.0f, 0.5f, 1.0f, 1.0f), // 蓝
        ImVec4(1.0f, 1.0f, 0.0f, 1.0f), // 黄
        ImVec4(1.0f, 0.0f, 1.0f, 1.0f), // 紫
        ImVec4(0.0f, 1.0f, 1.0f, 1.0f), // 青
        ImVec4(1.0f, 0.5f, 0.0f, 1.0f), // 橙
        ImVec4(0.5f, 0.5f, 0.5f, 1.0f)  // 灰
    };

    return colors[index % (sizeof(colors) / sizeof(colors[0]))];
}