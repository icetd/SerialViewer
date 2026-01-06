#ifndef SERIALPORT_LAYER_H
#define SERIALPORT_LAYER_H

#include <string>
#include <vector>
#include "../Core/Layer.h"
#include "../Utils/Utils.h"
#include "../Commuication/SerialPort.h"
#include "../UI/Oscilloscope.h"

class SerialPortLayer : public Layer
{
public:
    SerialPortLayer();

protected:
    virtual void OnAttach() override;
    virtual void OnUpdate(float ts) override;
    virtual void OnDetach() override;
    virtual void OnUIRender() override;

private:
    std::unique_ptr<SerialPort> m_serialPort = nullptr;
    int32_t m_select_port = 0;
    std::vector<serial::PortInfo> m_serilInfoList;

    Oscilloscope m_scope;

    void messageCallback(uint8_t *data, uint16_t len);
    void ShowPortControl();

    // 波特率设置相关
    std::vector<std::pair<int, std::string>> m_baudrate_list;
    std::vector<const char *> m_baudrate_display_list;
    int m_selected_baudrate;

    void InitBaudrateList();
};

#endif