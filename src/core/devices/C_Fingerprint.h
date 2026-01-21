#ifndef C_FINGERPRINT_H
#define C_FINGERPRINT_H

#include "C_Sensor.h"
#include <stdint.h>

class C_UART;
class C_GPIO;

#define FINGER_HEAD         0xF5
#define FINGER_TAIL         0xF5
#define CMD_ADD_1           0x01 
#define CMD_ADD_2           0x02 
#define CMD_ADD_3           0x03 
#define CMD_DEL             0x04 
#define CMD_MATCH           0x0C 

#define ACK_SUCCESS         0x00
#define ACK_FAIL            0x01
#define ACK_FULL            0x04
#define ACK_NO_USER         0x05
#define ACK_TIMEOUT         0x08

class C_Fingerprint final : public C_Sensor {
public:
    C_Fingerprint(C_UART& uart, C_GPIO& rst);
    ~C_Fingerprint() override;

    
    bool init() override;
    
    bool read(SensorData* data) override;
    
    void wakeUp();
    void sleep();
    
    bool addUser(int userID);
    bool deleteUser(int userID);
    bool deleteAllUsers();

private:
    C_UART& m_uart;
    C_GPIO& m_rst; 

    uint8_t executeCommand(uint8_t cmd, uint8_t p1, uint8_t p2,
        uint8_t p3, uint8_t* outHigh, uint8_t* outLow, float timeoutSec) const;
};

#endif
