/*
 * Fan actuator implementation.
 */

#include "C_Fan.h"
#include "C_GPIO.h"

C_Fan::C_Fan(C_GPIO &gpio_fan) : C_Actuator(ID_FAN),  gpio_fan(gpio_fan), fan_on(false){}

bool C_Fan::init() {
    // Initialize GPIO and ensure OFF state.
    if (!gpio_fan.init()) {
        return false;
    }
    stop();
    return true;
}
C_Fan::~C_Fan() {
    C_Fan::stop();
}
void C_Fan::stop() {
    // Physically turn off the GPIO.
    gpio_fan.writePin(false);
    fan_on = false;
}

bool C_Fan::set_value(const uint8_t value) {
    // Any value > 0 turns the fan on.
    if (value > 0) {
        gpio_fan.writePin(true);
        fan_on = true;
    }
    else {
        stop();
    }
    return true;
}
