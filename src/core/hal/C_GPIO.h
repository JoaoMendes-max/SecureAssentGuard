#ifndef C_GPIO_H
#define C_GPIO_H

#include <string>

using namespace std;

enum GPIO_DIRECTION { IN, OUT };

class C_GPIO {
public:
    C_GPIO(int pin, GPIO_DIRECTION dir);
    ~C_GPIO();
    bool init();
    void closePin();
    void writePin(bool value);
    bool readPin();
private:
    int m_pin;
    GPIO_DIRECTION m_dir;
    string m_path;
};

#endif 