// SerialPort.cpp 修改 - 只改构造函数
#include "SerialPort.h"
#include "../Core/log.h"

// 原构造函数 - 保持向后兼容
SerialPort::SerialPort(const std::string port, uint32_t baudrate, uint32_t timeout) : 
    m_portName(port),
    m_baudrate(baudrate),
    m_timeout(timeout),
    m_bytesize(serial::eightbits),      // 默认8数据位
    m_parity(serial::parity_none),      // 默认无校验
    m_stopbits(serial::stopbits_one),   // 默认1停止位
    m_flowcontrol(serial::flowcontrol_none) // 默认无流控
{
    m_serial = new serial::Serial(m_portName, baudrate, serial::Timeout::simpleTimeout(timeout));
}

// 新增：完整参数的构造函数
SerialPort::SerialPort(const std::string port, 
                       uint32_t baudrate, 
                       uint32_t timeout,
                       serial::bytesize_t bytesize,
                       serial::parity_t parity,
                       serial::stopbits_t stopbits,
                       serial::flowcontrol_t flowcontrol) : 
    m_portName(port),
    m_baudrate(baudrate),
    m_timeout(timeout),
    m_bytesize(bytesize),
    m_parity(parity),
    m_stopbits(stopbits),
    m_flowcontrol(flowcontrol)
{
    // 使用完整参数的构造函数
    m_serial = new serial::Serial(m_portName, 
                                  m_baudrate, 
                                  serial::Timeout::simpleTimeout(m_timeout),
                                  m_bytesize,
                                  m_parity,
                                  m_stopbits,
                                  m_flowcontrol);
    
    LOG(INFO, "SerialPort created: port=%s, baudrate=%d", m_portName.c_str(), m_baudrate);
}

// 其余所有函数保持不变
SerialPort::~SerialPort()
{
    if (m_serial->isOpen()) {
        m_serial->close();
    }
}

int SerialPort::Write(char *buf, int len)
{
    return m_serial->write((uint8_t* )buf, len);
}

void SerialPort::SetSerialHandler(std::function<void (uint8_t*, uint16_t)> callback)
{
    onSerialCallback = std::move(callback);
}

void SerialPort::StartAutoRead()
{
    this->start();
    this->detach();
    isAutoRead = true;
    m_serial->setTimeout(serial::Timeout::simpleTimeout(1000));
}

void SerialPort::StopAutoRead()
{
    isAutoRead = false;
    this->stop();
}

void SerialPort::run()
{
    std::string result;

    while (!this->isStoped()) {
        result = m_serial->readline(65536Ui64, ";");
        if (result.length()) {
            try {
                onSerialCallback((uint8_t*)result.c_str(), result.length());
            } catch (const std::exception &ex) {
                LOG(ERRO, "Exception in disconnect callback: %s", ex.what());
            }
        }

    }
    if (m_serial->isOpen())
        m_serial->close();
}