#include "C_alarmActuator.h"
#include "C_GPIO.h"

C_alarmActuator::C_alarmActuator(C_GPIO& gpio_led, C_GPIO& gpio_buzzer)
: C_Actuator(ID_ALARM_ACTUATOR), gpio_led(gpio_led), gpio_buzzer(gpio_buzzer), ison(false) {}

C_alarmActuator::~C_alarmActuator() {
    C_alarmActuator::stop();
}

void C_alarmActuator::stop() {
    gpio_buzzer.writePin(false);
    gpio_led.writePin(false);
    ison = false;
}
bool C_alarmActuator::init() {
    if (!gpio_led.init()) {
        return false;
    }
    if (!gpio_buzzer.init()) {
        return false;
    }
    stop();
    return true;
}

bool C_alarmActuator::set_value(uint8_t value) {
    if (value > 0) {
        gpio_buzzer.writePin(true);
        gpio_led.writePin(true);
        ison = true;
    }
    else {
        stop();
    }
    return true;
}


