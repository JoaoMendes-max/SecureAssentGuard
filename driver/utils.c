#include "utils.h"
#include <linux/module.h>

void SetGPIOFunction(struct GpioRegisters *s_pGpioRegisters, int GPIO, int functionCode) {
	int registerIndex = GPIO / 10; // cada registo controla 10 GPIOs
	int bit = (GPIO % 10) * 3;     // primeiro dos 3 bits do pino dentro do registo

	unsigned oldValue = s_pGpioRegisters->GPFSEL[registerIndex];
	unsigned mask = 0b111 << bit;

	// limpa os 3 bits com a máscara e escreve o functionCode no sítio
	s_pGpioRegisters->GPFSEL[registerIndex] = (oldValue & ~mask) | ((functionCode << bit) & mask);
}

void SetGPIOPullUpDown(struct GpioRegisters *s_pGpioRegisters, int GPIO, int pud) {
	int registerIndex = GPIO / 16; // cada registo controla 16 pinos
	int bit = (GPIO % 16) * 2;     // cada pino usa 2 bits

	unsigned int oldValue = s_pGpioRegisters->PUP_PDN[registerIndex];
	unsigned int mask = 0b11 << bit;

	s_pGpioRegisters->PUP_PDN[registerIndex] = (oldValue & ~mask) | ((pud << bit) & mask);
}
