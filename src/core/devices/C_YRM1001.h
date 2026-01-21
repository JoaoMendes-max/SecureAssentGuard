#ifndef C_YRM1001_H
#define C_YRM1001_H

#include "C_Sensor.h"
#include <stdint.h>
#include <cstddef>
class C_UART;
class C_GPIO;

/* ============================================
 * PROTOCOL DEFINITIONS
 * ============================================ */
#define YRM_HEADER          0xBB
#define YRM_TAIL            0x7E
#define YRM_TYPE_NOTIF      0x02
#define YRM_CMD_INVENTORY   0x22

// Frame offsets
#define YRM_IDX_HEADER      0
#define YRM_IDX_TYPE        1
#define YRM_IDX_COMMAND     2
#define YRM_IDX_PL_MSB      3
#define YRM_IDX_PL_LSB      4
#define YRM_IDX_PAYLOAD     5

// Timing constants
#define YRM_BOOT_TIME_MS    100
#define YRM_STOP_TIME_MS    50
#define YRM_IDLE_TIMEOUT_MS 500

/* ============================================
 * C_YRM1001 CLASS
 * UHF RFID Reader for Inventory Management
 * ============================================ */
class C_YRM1001 final : public C_Sensor {
private:
    C_UART& m_uart;
    C_GPIO& m_gpio_enable;

    uint8_t m_rawBuffer[256];
    int m_bufferPos;

    static const int MAX_TAGS = 4;

public:
    C_YRM1001(C_UART& uart, C_GPIO& enable);
    ~C_YRM1001() override;

    // Inherited from C_Sensor
    bool init() override;
    bool read(SensorData* data) override;

private:
    /* Power Management */
    bool powerOn();
    void powerOff();
    bool getPower(uint16_t& outCentiDbm);

    /* Communication */
    bool sendCommand(const uint8_t* cmd, size_t len) const;
    bool readFrame();

    /* Parsing */
    bool parseFrame(char* epcOut, size_t epcSize) const;

    /* Helper Functions */
    static uint8_t calculateChecksum(const uint8_t* data, size_t len);
    bool setPower(uint16_t powerDBm);
    static void bytesToHex(const uint8_t* data, size_t len, char* hexOut);
    static bool isTagSeen(const char* epc, char tagList[][32], int tagCount) ;
    void flushUART() const;
};

#endif // C_YRM1001_H