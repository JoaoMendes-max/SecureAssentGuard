# Hardware Reference

Central unit: Raspberry Pi 4 Model B. The 5 V peripherals interface through an 8-channel
bidirectional logic level converter. Servos and fan are driven through MOSFETs.

## GPIO pinout

| GPIO | Function | Peripheral |
|------|----------|------------|
| 2  | I2C SDA      | Temp/Humidity (SHT30) |
| 3  | I2C SCL      | Temp/Humidity (SHT30) |
| 14 | UART0 TX     | Fingerprint sensor |
| 15 | UART0 RX     | Fingerprint sensor |
| 0  | UART2 TX     | RFID entry reader |
| 1  | UART2 RX     | RFID entry reader |
| 4  | UART3 TX     | RFID exit reader |
| 5  | UART3 RX     | RFID exit reader |
| 8  | UART4 TX     | YRM1001 inventory reader |
| 9  | UART4 RX     | YRM1001 inventory reader |
| 12 | PWM          | Servo, room door |
| 13 | PWM          | Servo, vault door |
| 18 | OUTPUT       | 12 V fan |
| 17 | INPUT        | PIR motion sensor |
| 27 | INPUT        | Reed switch, room door |
| 22 | INPUT        | Reed switch, vault door |
| 6  | INPUT        | Fingerprint WAKE |
| 23 | OUTPUT       | Active buzzer |
| 24 | OUTPUT       | Alert LED |
| 25 | OUTPUT       | YRM1001 enable |
| 26 | OUTPUT       | Fingerprint sleep/reset |

SHT30 I2C address: 0x44 (bus 1). UART devices map to /dev/ttyAMA{n}.

## Components

| Component | Model | Interface |
|-----------|-------|-----------|
| Board | Raspberry Pi 4 Model B | - |
| Temp/Humidity | SHT30 | I2C |
| Fingerprint | Waveshare UART Capacitive (Type D) | UART |
| RFID access (125 kHz) | RDM6300 | UART |
| RFID inventory (UHF) | YRM1001 | UART |
| Motion | HC-SR501 PIR | GPIO |
| Door state | Reed switch | GPIO |
| Door actuation | MG996R servo | PWM |
| Cooling | 12 V fan | GPIO and MOSFET |
| Alerts | Active buzzer and LED | GPIO and MOSFET |

## Power

```
Grid --+-- 12 V 6 A PSU --+-- DC-DC 5 V  --> 5 V sensors and actuators
       |                  +-- DC-DC 3.3 V --> 3.3 V sensors
       |                  +-- 12 V fan
       +-- USB-C 5 V 3 A --> Raspberry Pi 4
```

## Kernel IRQ driver

Inputs (PIR, reed switches, fingerprint WAKE, RFID activity) are handled by a custom character
device driver (/dev/irq0-5) that maps GPIO interrupts to Real-Time Signals (43 to 48) with a
500 ms kernel debounce, removing the need for polling. The launcher loads it via
`insmod /root/my_irq.ko` at startup.

Source and build instructions are in [../driver/](../driver/). Signal mapping:

| Signal | GPIO | Source | Edge |
|--------|------|--------|------|
| 43 | 22 | reed_switch_vault | rising |
| 44 | 27 | reed_switch_room  | rising |
| 45 | 17 | pir_sensor        | rising |
| 46 | 6  | fingerprint       | rising |
| 47 | 16 | rfid_entry        | falling |
| 48 | 20 | rfid_left         | falling |

The payload of each signal (si_int) carries the GPIO pin number that triggered it.
