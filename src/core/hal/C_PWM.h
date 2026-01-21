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
    
    C_PWM(int chip, int channel);
    
    ~C_PWM();
    
    bool init();
    
    bool setPeriodns(int s);
    
    bool setDutyCycle(uint8_t duty);
    
    bool setEnable(bool enable);
};
#endif 