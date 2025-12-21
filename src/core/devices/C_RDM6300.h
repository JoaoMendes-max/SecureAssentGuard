#ifndef C_RDM6300_H
#define C_RDM6300_H

#include "C_Sensor.h"
#include <stdint.h>

class C_UART;

#define RDM_STX         0x02
#define RDM_ETX         0x03
#define RDM_FRAME_SIZE  14

class C_RDM6300 final : public C_Sensor {
public:
    C_RDM6300(C_UART& uart);
    ~C_RDM6300() override;

    bool init() override;
    bool read(SensorData* data) override;

private:
    C_UART& m_uart;
    char m_rawBuffer[RDM_FRAME_SIZE]{};

    static uint8_t asciiCharToVal(char c);
    static uint8_t hexPairToByte(char high, char low);
    static bool validateChecksum(const char* buffer);
    static void parseTag(const char* buffer, char* dest);
    bool waitForData(int timeout_ms) const;
};
#endif

