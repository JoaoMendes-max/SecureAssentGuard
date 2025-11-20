#ifndef UNTITLED_C_PWM_H
#define UNTITLED_C_PWM_H
#include <cstdint>

class C_PWM
{
private:
    int m_pwmChip;
    int m_pwmChannel;
    int m_fd_period;
    uint8_t m_fd_duty;
    bool m_fd_enable;
public:
    // Constructor
    C_PWM(int chip, int channel);
    // Destructor
    ~C_PWM();
    // inicialization
    bool init();
    // period
    bool setPeriodns(int s);
    //duty cyle percentage
    bool setDutyCycle(uint8_t duty);
    // activates/deactivates pwm
    bool setEnable(bool enable);
};
#endif //UNTITLED_C_PWM_H