#ifndef C_FAN_H
#define C_FAN_H

#include "C_Actuator.h"
#include "C_GPIO.h"

class C_Fan final: public C_Actuator {

public:
    C_Fan(C_GPIO& gpio_fan);
    ~C_Fan() override;
    bool init() override;
    bool set_value(uint8_t value) override;
    void stop() override;
    bool isON() const {return fan_on;}
private:
    C_GPIO& gpio_fan;
    bool fan_on;
};

#endif
