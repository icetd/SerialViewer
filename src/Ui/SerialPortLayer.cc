#include "SerialPortLayer.h"
#include "imgui.h"
#include <sstream>
#include <functional>
#include <iostream>
#include <vector>

SerialPortLayer::SerialPortLayer() : m_select_port(0), m_serialPort(nullptr), m_selected_baudrate(21) // 默认选择115200
{
}

void SerialPortLayer::OnAttach()
{
    try {
        m_serilInfoList = serial::list_ports();
    } catch (const std::exception &e) {
        std::cerr << "Error listing ports: " << e.what() << std::endl;
        m_serilInfoList.clear();
    }

    // 初始化波特率列表
    InitBaudrateList();

    // 初始化示波器通道名称
    m_scope.SetChannelName(0, u8"通道1");
    m_scope.SetChannelName(1, u8"通道2");
    m_scope.SetChannelName(2, u8"通道3");
    m_scope.SetChannelName(3, u8"通道4");

    std::cout << "串口层初始化完成，示波器已准备就绪" << std::endl;
}

void SerialPortLayer::InitBaudrateList()
{
    // 完整的波特率列表
    m_baudrate_list = {
        {50, "50"},
        {75, "75"},
        {110, "110"},
        {134, "134"},
        {150, "150"},
        {200, "200"},
        {300, "300"},
        {600, "600"},
        {1200, "1200"},
        {1800, "1800"},
        {2400, "2400"},
        {4800, "4800"},
        {7200, "7200"},
        {9600, "9600"},
        {14400, "14400"},
        {19200, "19200"},
        {28800, "28800"},
        {38400, "38400"},
        {56000, "56000"},
        {57600, "57600"},
        {76800, "76800"},
        {115200, "115200"},
        {128000, "128000"},
        {153600, "153600"},
        {230400, "230400"},
        {256000, "256000"},
        {460800, "460800"},
        {500000, "500000"},
        {576000, "576000"},
        {921600, "921600"},
        {1000000, "1000000"},
        {1152000, "1152000"},
        {1500000, "1500000"},
        {2000000, "2000000"},
        {2500000, "2500000"},
        {3000000, "3000000"},
        {3500000, "3500000"},
        {4000000, "4000000"}};

    // 波特率显示文本
    m_baudrate_display_list.clear();
    for (const auto &baud : m_baudrate_list) {
        m_baudrate_display_list.push_back(baud.second.c_str());
    }
}

void SerialPortLayer::OnUpdate(float ts)
{
    ShowPortControl();
}

void SerialPortLayer::OnDetach()
{
    if (m_serialPort) {
        try {
            std::cout << "正在断开串口连接..." << std::endl;
            m_serialPort->StopAutoRead();
        } catch (const std::exception &e) {
            std::cerr << "停止自动读取时出错: " << e.what() << std::endl;
        }

        std::cout << "串口已断开" << std::endl;
    }
}

void SerialPortLayer::OnUIRender()
{
}

void SerialPortLayer::ShowPortControl()
{
    // 串口控制窗口
    ImGui::SetNextWindowSize(ImVec2(550, 350), ImGuiCond_FirstUseEver);
    ImGui::Begin(u8"串口通信设置");

    // 串口设备选择
    ImGui::Text(u8"串口设备选择");
    ImGui::Separator();

    // 刷新按钮
    if (ImGui::Button(u8"刷新设备列表", ImVec2(120, 30))) {
        try {
            m_serilInfoList = serial::list_ports();
            std::cout << "刷新串口列表，找到 " << m_serilInfoList.size() << " 个端口" << std::endl;
        } catch (const std::exception &e) {
            std::cerr << "刷新串口列表时出错: " << e.what() << std::endl;
            m_serilInfoList.clear();
        }
    }

    ImGui::SameLine();

    // 串口列表
    std::vector<const char *> ports;
    for (auto &p : m_serilInfoList) {
        ports.push_back(p.port.c_str());
    }

    if (!ports.empty()) {
        ImGui::SetNextItemWidth(200);
        ImGui::Combo(u8"选择串口", &m_select_port, ports.data(), static_cast<int>(ports.size()));
    } else {
        ImGui::TextColored(ImVec4(1, 0, 0, 1), u8"未检测到串口设备");
    }

    ImGui::Spacing();

    // 串口参数设置
    ImGui::SeparatorText(u8"串口参数设置");

    // 波特率选择
    ImGui::SetNextItemWidth(120);
    ImGui::Combo(u8"波特率", &m_selected_baudrate, m_baudrate_display_list.data(),
                 static_cast<int>(m_baudrate_display_list.size()));

    // 数据位选择
    static int data_bits = 3; // 8数据位
    ImGui::SetNextItemWidth(100);
    ImGui::Combo(u8"数据位", &data_bits, u8"5\0 6\0 7\0 8\0");

    // 停止位选择
    static int stop_bits = 0; // 1停止位
    ImGui::SetNextItemWidth(100);
    ImGui::Combo(u8"停止位", &stop_bits, u8"1\0 1.5\0 2\0");

    // 校验位选择
    static int parity = 0; // 无校验
    ImGui::SetNextItemWidth(100);
    ImGui::Combo(u8"校验位", &parity, u8"无\0 奇\0 偶\0");

    // 显示当前选择的波特率数值
    if (!m_baudrate_list.empty() && m_selected_baudrate >= 0 && m_selected_baudrate < static_cast<int>(m_baudrate_list.size())) {
        ImGui::SameLine();
        ImGui::TextDisabled(u8"(%d bps)", m_baudrate_list[m_selected_baudrate].first);
    }

    ImGui::Spacing();

    // 连接控制
    ImGui::SeparatorText(u8"连接控制");

    // 连接/断开按钮
    static bool opened = false;

    if (ImGui::Button(opened ? u8"断开连接" : u8"连接设备", ImVec2(120, 40))) {
        opened = !opened;

        if (opened && !ports.empty()) {
            try {
                std::string port_name = ports[m_select_port];
                int baudrate = m_baudrate_list[m_selected_baudrate].first;

                // 解析数据位
                serial::bytesize_t bytesize = serial::eightbits;
                switch (data_bits) {
                case 0: bytesize = serial::fivebits; break;
                case 1: bytesize = serial::sixbits; break;
                case 2: bytesize = serial::sevenbits; break;
                case 3: bytesize = serial::eightbits; break;
                }

                // 解析停止位
                serial::stopbits_t stopbits = serial::stopbits_one;
                switch (stop_bits) {
                case 0: stopbits = serial::stopbits_one; break;
                case 1: stopbits = serial::stopbits_one_point_five; break;
                case 2: stopbits = serial::stopbits_two; break;
                }

                // 解析校验位
                serial::parity_t parity_type = serial::parity_none;
                switch (parity) {
                case 0: parity_type = serial::parity_none; break;
                case 1: parity_type = serial::parity_odd; break;
                case 2: parity_type = serial::parity_even; break;
                }

                std::cout << "正在打开串口: " << port_name
                          << " 波特率: " << baudrate
                          << " 数据位: " << (data_bits + 5)
                          << " 停止位: " << (stop_bits == 0 ? "1" : stop_bits == 1 ? "1.5" :
                                                                                     "2")
                          << " 校验位: " << (parity == 0 ? "无" : parity == 1 ? "奇" :
                                                                                "偶")
                          << std::endl;

                // 使用完整参数的构造函数
                m_serialPort = std::make_unique<SerialPort>(port_name, baudrate, 1000,
                                                            bytesize, parity_type, stopbits,
                                                            serial::flowcontrol_none);

                m_serialPort->SetSerialHandler(
                    std::bind(
                        &SerialPortLayer::messageCallback,
                        this,
                        std::placeholders::_1,
                        std::placeholders::_2));
                m_serialPort->StartAutoRead();
                std::cout << "串口打开成功，等待数据..." << std::endl;
            } catch (const std::exception &e) {
                std::cerr << "打开串口失败: " << e.what() << std::endl;
                opened = false;
            }
        } else if (!opened && m_serialPort) {
            try {
                std::cout << "正在关闭串口..." << std::endl;
                m_serialPort->StopAutoRead();
            } catch (const std::exception &e) {
                std::cerr << "关闭串口时出错: " << e.what() << std::endl;
            }

            std::cout << "串口已关闭" << std::endl;
        } else if (!opened) {
            // 如果点击断开但 m_serialPort 为空，确保 opened 状态正确
            opened = false;
        }
    }

    ImGui::SameLine();

    // 状态显示
    if (opened && m_serialPort) {
        ImGui::TextColored(ImVec4(0, 1, 0, 1), u8"状态: 已连接");
        ImGui::SameLine();
        ImGui::TextDisabled(u8"设备: %s", ports[m_select_port]);

        // 显示当前串口参数
        if (!m_baudrate_list.empty() && m_selected_baudrate >= 0 && m_selected_baudrate < static_cast<int>(m_baudrate_list.size())) {
            ImGui::TextDisabled(u8"波特率: %d  数据位: %d  停止位: %s  校验位: %s",
                                m_baudrate_list[m_selected_baudrate].first,
                                data_bits + 5,
                                stop_bits == 0 ? "1" : stop_bits == 1 ? "1.5" :
                                                                        "2",
                                parity == 0 ? u8"无" : parity == 1 ? u8"奇" :
                                                                     u8"偶");
        }
    } else {
        ImGui::TextColored(ImVec4(1, 0, 0, 1), u8"状态: 未连接");
        ImGui::SameLine();
        ImGui::TextDisabled(u8"等待连接...");
    }

    ImGui::End(); // 结束串口控制窗口

    // 示波器窗口 - 单独窗口
    ImGui::SetNextWindowSize(ImVec2(1000, 700), ImGuiCond_FirstUseEver);
    m_scope.Draw();
}

void SerialPortLayer::messageCallback(uint8_t *data, uint16_t len)
{
    // 这个函数在串口线程中被调用
    static std::string cache;
    static int data_counter = 0;

    // 简单统计
    data_counter += len;
    if (data_counter > 10000) { // 每收到10KB数据输出一次
        std::cout << "串口线程: 收到数据，累计 " << data_counter << " 字节" << std::endl;
        data_counter = 0;
    }

    // 添加到缓存
    cache.append(reinterpret_cast<char *>(data), len);

    // 处理所有完整的数据帧
    int frames_processed = 0;
    const int MAX_FRAMES_PER_CALL = 20; // 防止一次处理太多帧

    while (frames_processed < MAX_FRAMES_PER_CALL) {
        // 查找起始符
        size_t s = cache.find('$');
        if (s == std::string::npos) {
            // 如果缓存太大，清空
            if (cache.size() > 4096) {
                cache.clear();
                std::cout << "串口线程: 缓存过大，已清空" << std::endl;
            }
            break;
        }

        // 查找结束符
        size_t e = cache.find(';', s);
        if (e == std::string::npos) {
            // 保留可能包含起始符的部分数据
            if (s > 0) {
                cache.erase(0, s);
            }
            // 如果缓存太大，清空
            if (cache.size() > 4096) {
                cache.clear();
                std::cout << "串口线程: 缓存过大，已清空" << std::endl;
            }
            break;
        }

        // 提取有效载荷
        std::string payload = cache.substr(s + 1, e - s - 1);
        cache.erase(0, e + 1);

        frames_processed++;

        // 解析浮点数
        std::stringstream ss(payload);
        std::vector<float> values;
        float v;

        while (ss >> v) {
            values.push_back(v);
        }

        // 如果有数据，添加到示波器（双重缓冲，线程安全）
        if (!values.empty()) {
            static int frame_counter = 0;
            frame_counter++;

            // 每100帧输出一次调试信息
            if (frame_counter % 100 == 0) {
                std::cout << "串口线程: 解析到第 " << frame_counter
                          << " 帧，包含 " << values.size() << " 个数据点" << std::endl;
                std::cout << "      示例数据: ";
                for (size_t i = 0; i < std::min(values.size(), size_t(3)); ++i) {
                    std::cout << values[i] << " ";
                }
                std::cout << "..." << std::endl;
            }

            // 添加到示波器
            m_scope.AddFrame(values);
        }

        // 如果缓存中还剩很多数据，继续处理
        if (cache.size() > 1024) {
            continue; // 继续处理下一帧
        } else {
            break; // 缓存数据不多，退出循环
        }
    }
}