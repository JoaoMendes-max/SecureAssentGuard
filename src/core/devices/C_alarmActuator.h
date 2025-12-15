#ifndef C_ALARMACTUATOR_H
#define C_ALARMACTUATOR_H
#include "C_Actuator.h"
#include "SharedTypes.h"

class C_GPIO;

class C_alarmActuator final : public C_Actuator {

public:
    C_alarmActuator(C_GPIO& gpio_led, C_GPIO& gpio_buzzer);
    ~C_alarmActuator() override;
    bool init() override;
    bool set_value(uint8_t value) override;
    void stop() override;
    bool isON() const {return ison; }
private:
    C_GPIO& gpio_led;
    C_GPIO& gpio_buzzer;
    bool ison;
};

#endif
