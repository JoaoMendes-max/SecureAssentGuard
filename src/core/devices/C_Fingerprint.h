#ifndef C_FINGERPRINT_H
#define C_FINGERPRINT_H

#include "C_Sensor.h"
#include <stdint.h>

class C_UART;
class C_GPIO;

// Protocol Definitions (According to User Manual)
#define FINGER_HEAD         0xF5
#define FINGER_TAIL         0xF5
#define CMD_ADD_1           0x01 // Add User Step 1
#define CMD_ADD_2           0x02 // Add User Step 2
#define CMD_ADD_3           0x03 // Add User Step 3
#define CMD_DEL             0x04 // Delete User
#define CMD_MATCH           0x0C // 1:N Match (Verify)

// Response Status Codes
#define ACK_SUCCESS         0x00
#define ACK_FAIL            0x01
#define ACK_FULL            0x04
#define ACK_NO_USER         0x05
#define ACK_TIMEOUT         0x08

class C_Fingerprint final : public C_Sensor {

public:
    C_Fingerprint(C_UART& uart, C_GPIO& rst);
    ~C_Fingerprint() override;

    // Hardware initialization (UART configuration)
    bool init() override;

    // Verification Logic
    bool read(SensorData* data) override;

    // Power Management (To be used by the Thread)
    void wakeUp();
    void sleep();

    // Management Functions
    bool addUser(int userID);
    bool deleteUser(int userID);

private:

    C_UART& m_uart;
    C_GPIO& m_rst; // Reset Pin to control Power/Sleep state

    uint8_t executeCommand(uint8_t cmd, uint8_t p1, uint8_t p2,
        uint8_t p3, uint8_t* outHigh, uint8_t* outLow, float timeoutSec) const;

};

#endif
