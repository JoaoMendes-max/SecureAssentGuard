#include <linux/types.h>

#define BCM2708_PERI_BASE       0xfe000000
#define GPIO_BASE (BCM2708_PERI_BASE + 0x200000) // GPIO controller

struct GpioRegisters
{
    // Offset 0x00: Seleção de Função (Input/Output)
    uint32_t GPFSEL[6];

    // Offset 0x18: salta GPSET, GPCLR, GPLEV, etc.
    uint32_t ReservedGap[51];

    // Offset 0xE4: Controlos de Pull-Up / Pull-Down (RPi 4)
    uint32_t PUP_PDN[4];
};

void SetGPIOFunction(struct GpioRegisters *s_pGpioRegisters, int GPIO, int functionCode);
void SetGPIOPullUpDown(struct GpioRegisters *s_pGpioRegisters, int GPIO, int pud);
