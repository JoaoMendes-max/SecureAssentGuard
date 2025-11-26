
#include "C_PWM.h"
#include <fcntl.h>
#include <unistd.h>
#include <string>
#include <iostream>
using namespace std;

C_PWM::C_PWM(int chip, int channel)
    : m_pwmChip(chip), m_pwmChannel(channel),
      m_fd_period(0), m_fd_duty(0),m_fd_enable(false)
{
}

C_PWM::~C_PWM()
{
    setEnable(false);
}

bool C_PWM::init()
{
    string exportPath = "/sys/class/pwm/pwmchip" + to_string(m_pwmChip)+ "/export";

    int fd = open(exportPath.c_str(), O_WRONLY);
    if (fd < 0)
    {
        cerr << "Erro ao abrir export\n";
        return false;
    }

    string channel = to_string(m_pwmChannel);
    if (write(fd, channel.c_str(), channel.size()) < 0)
    {
        cerr << "Erro ao exportar PWM\n";
        close(fd);
        return false;
    }

    close(fd);
    return true;
}

bool C_PWM::setPeriodns(int s) {
    m_fd_period = s;
    string periodPath =
            "/sys/class/pwm/pwmchip" + to_string(m_pwmChip) +
            "/pwm" + to_string(m_pwmChannel) + "/period";
    int fd = open(periodPath.c_str(), O_WRONLY);
    if (fd < 0)
    {
        cerr << "Erro ao abrir period\n";
        return false;
    }
    string val = to_string(s);
    if (write(fd, val.c_str(), val.size()) < 0) {
        cerr << "Erro ao escrever period\n";
        close(fd);
        return false;
    }
    close(fd);
    return true;
}


bool C_PWM::setDutyCycle(uint8_t duty) {
    if (duty > 100) duty = 100;
    m_fd_duty = duty;
    int dutyns = (m_fd_period * duty) / 100;

    // Caminho para o ficheiro duty_cycle
    string dutyPath =
            "/sys/class/pwm/pwmchip" + to_string(m_pwmChip) +
            "/pwm" + to_string(m_pwmChannel) + "/duty_cycle";
    int fd = open(dutyPath.c_str(), O_WRONLY);
    if (fd < 0) {
        cerr << "Erro ao abrir duty_cycle\n";
        return false;
    }

    string val = to_string(dutyns);
    if (write(fd, val.c_str(), val.size()) < 0) {
        cerr << "Erro ao escrever duty_cycle\n";
        close(fd);
        return false;
    }

    close(fd);
    return true;
}


bool C_PWM::setEnable(bool enable)
{
    m_fd_enable = enable;
    string enablePath =
            "/sys/class/pwm/pwmchip" + to_string(m_pwmChip) +
            "/pwm" + to_string(m_pwmChannel) + "/enable";

    int fd = open(enablePath.c_str(), O_WRONLY);
    if (fd < 0)
    {
        cerr << "Erro ao abrir enable\n";
        return false;
    }
    string val = enable ? "1" : "0";
    if (write(fd, val.c_str(), val.size()) < 0)
    {
        cerr << "Erro ao escrever enable\n";
        close(fd);
        return false;
    }
    close(fd);
    return true;
}
