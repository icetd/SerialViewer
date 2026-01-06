// SerialPort.h 修改
#ifndef SERIAL_PORT_H
#define SERIAL_PORT_H

#include <string>
#include <functional>
#include <stdint.h>
#include "Serial.h"
#include "../Core/MThread.h"

class SerialPort : public MThread
{
public:
    SerialPort(const std::string port = "", uint32_t baudrate = 115200, uint32_t timeout = 1000);
    
    SerialPort(const std::string port, 
               uint32_t baudrate, 
               uint32_t timeout,
               serial::bytesize_t bytesize,
               serial::parity_t parity,
               serial::stopbits_t stopbits,
               serial::flowcontrol_t flowcontrol);
    
    ~SerialPort();
    
    int Write(char *buf, int len);

    void SetSerialHandler(std::function<void (uint8_t*, uint16_t)> callback);
    void StartAutoRead();
    void StopAutoRead();
    virtual void run() override;

    bool isWorking () { return isAutoRead; }

private:
    std::string m_portName;
    uint32_t m_baudrate;
    uint32_t m_timeout;
    
    // 新增串口参数
    serial::bytesize_t m_bytesize;
    serial::parity_t m_parity;
    serial::stopbits_t m_stopbits;
    serial::flowcontrol_t m_flowcontrol;

    serial::Serial *m_serial;

    bool isAutoRead = false;
    std::function<void(uint8_t *, uint16_t)> onSerialCallback;
};

#endif