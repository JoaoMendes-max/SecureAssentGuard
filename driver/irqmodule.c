#include <linux/cdev.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/init.h>
#include <asm/io.h>
#include <linux/timer.h>
#include <linux/err.h>
#include <linux/mm.h>
#include <linux/gpio.h>
#include <linux/io.h>
#include <linux/interrupt.h>

#include "utils.h"

#define IRQ_IOC_MAGIC  'k'
#define REGIST_PID     _IOW(IRQ_IOC_MAGIC, 1, int)

/* .............................................................................
   The SensorConfig structure encapsulates the hardware and software properties
   of each security peripheral. It maps physical GPIO pins to specific
   Real-Time Signals and maintains state variables for kernel-level debouncing.
   .............................................................................*/
struct SensorConfig {
    int gpio_pin;
    unsigned long trigger;
    int signal_id;
    char *name;
    bool enabled;
    unsigned long last_time;
};

/* .............................................................................
   Initialization of the my_sensors array. This table defines the configuration
   for all security inputs, including Reed switches, PIR motion sensors, and
   activation pins for the biometric and RFID modules.
   ............................................................................. */
static struct SensorConfig my_sensors[] = {
    {22, IRQF_TRIGGER_RISING,  43, "reed_switch_vault", true, 0},
    {27, IRQF_TRIGGER_RISING,  44, "reed_switch_room",  true, 0},
    {17, IRQF_TRIGGER_RISING,  45, "pir_sensor",        true, 0},
    { 6, IRQF_TRIGGER_RISING,  46, "fingerprint",       true, 0},
    {16, IRQF_TRIGGER_FALLING, 47, "rfid_entry",        true, 0},
    {20, IRQF_TRIGGER_FALLING, 48, "rfid_left",         true, 0}
};

#define DEVICE_NAME "irq"
#define CLASS_NAME "irqClass"
#define NUM_IRQS 6

static unsigned int irq_numbers[NUM_IRQS];

MODULE_LICENSE("GPL");

/* Device variables */
static struct class* irqDevice_class = NULL;
static dev_t irqDevice_majorminor;
static struct cdev c_dev;                  // estrutura do char device
static struct task_struct *task = NULL;    // processo registado para receber sinais
static struct GpioRegisters *s_pGpioRegisters;


// ==================================================================
// SYSTEM CALL: WRITE (Ligar/Desligar Interrupções)
// ==================================================================
static ssize_t irq_device_write(struct file *pfile, const char __user *pbuff, size_t len, loff_t *off) {
    char command;
    unsigned long minor;
    struct SensorConfig *sensor;

    // 1. Recuperar qual é o sensor (guardado no open)
    minor = (unsigned long)pfile->private_data;
    sensor = &my_sensors[minor];

    // 2. Ler o comando do utilizador (apenas 1 byte: '0' ou '1')
    if (copy_from_user(&command, pbuff, 1)) return -EFAULT;

    pr_alert("irqModule: Comando '%c' recebido para %s (IRQ %d)\n",
             command, sensor->name, irq_numbers[minor]);

    // 3. Lógica Ligar/Desligar
    if (command == '0') {
        // Só desativamos se estiver ativado (para evitar crash do kernel)
        if (sensor->enabled == true) {
            disable_irq(irq_numbers[minor]);
            sensor->enabled = false;
            pr_alert("irqModule: Interrupção DESATIVADA para %s\n", sensor->name);
        }
    }
    else if (command == '1') {
        // Só ativamos se estiver desativado
        if (sensor->enabled == false) {
            enable_irq(irq_numbers[minor]);
            sensor->enabled = true;
            pr_alert("irqModule: Interrupção ATIVADA para %s\n", sensor->name);
        }
    }
    else {
        pr_warn("irqModule: Comando inválido. Use '0' ou '1'.\n");
        return -EINVAL;
    }

    return len;
}

// CLOSE: Chamada quando a App fecha ou vai abaixo
static int irq_device_close(struct inode *p_inode, struct file * pfile) {
    // Se a App registada for a mesma que está a fechar o ficheiro, limpamos 'task'
    // para o handler não tentar enviar sinais a um processo morto.
    if (task != NULL && task == get_current()) {
        task = NULL;
        pr_alert("irqModule: A App fechou. Envio de sinais DESATIVADO.\n");
    }

    return 0;
}

static long irq_device_ioctl(struct file *file, unsigned int cmd, unsigned long arg) {
    // Regista o PID do processo que quer receber os sinais
    if (cmd == REGIST_PID) {
        task = get_current();
        pr_alert("irqModule: App registada! PID = %d. Sinais prontos a enviar.\n", task->pid);
    }
    else {
        return -EINVAL;
    }

    return 0;
}

static int irq_device_open(struct inode* p_inode, struct file *p_file) {
    // Descobrir qual o número do device (0, 1, 2...)
    int minor = iminor(p_inode);

    if (minor >= NUM_IRQS) return -ENODEV;

    // Guardar este número no private_data para o 'write' usar depois
    p_file->private_data = (void *)(unsigned long)minor;

    pr_alert("irqModule: Device irq%d aberto.\n", minor);
    return 0;
}

static struct file_operations irqDevice_fops = {
    .owner          = THIS_MODULE,
    .open           = irq_device_open,
    .release        = irq_device_close,
    .write          = irq_device_write,
    .read           = NULL,
    .unlocked_ioctl = irq_device_ioctl,
};

// ==================================================================
// INTERRUPT HANDLER
// ==================================================================
static irqreturn_t gpio_irq_handler(int irq, void *dev_id) {

    struct kernel_siginfo info;
    struct SensorConfig *sensor_atual = (struct SensorConfig *)dev_id;

    int pino = sensor_atual->gpio_pin;
    int sinal = sensor_atual->signal_id;

    unsigned long now = jiffies;

    // Debounce de 500 ms para filtrar ruído mecânico
    if ((now - sensor_atual->last_time) < msecs_to_jiffies(500)) {
        return IRQ_HANDLED;
    }

    pr_alert("irqModule: IRQ no %s (Pino %d)!\n", sensor_atual->name, pino);

    sensor_atual->last_time = now;

    if (task != NULL) {
        memset(&info, 0, sizeof(struct kernel_siginfo));
        info.si_signo = sinal;
        info.si_code = SI_QUEUE;
        info.si_int = pino;          // payload: pino que disparou

        if (send_sig_info(sinal, &info, task) < 0) {
            pr_err("irqModule: Erro ao enviar sinal.\n");
        }
    }

    return IRQ_HANDLED;
}


static int __init irqModule_init(void) {
    int ret, i;
    struct device *dev_ret;

    pr_alert("irqModule: A iniciar...\n");

    // 1. Alocar Major/Minor
    if ((ret = alloc_chrdev_region(&irqDevice_majorminor, 0, NUM_IRQS, DEVICE_NAME)) < 0) {
        return ret;
    }

    // 2. Criar Classe
    if (IS_ERR(irqDevice_class = class_create(CLASS_NAME))) {
        unregister_chrdev_region(irqDevice_majorminor, NUM_IRQS);
        return PTR_ERR(irqDevice_class);
    }

    // 3. Adicionar CDEV (ANTES dos devices!)
    cdev_init(&c_dev, &irqDevice_fops);
    c_dev.owner = THIS_MODULE;

    if ((ret = cdev_add(&c_dev, irqDevice_majorminor, NUM_IRQS)) < 0) {
        class_destroy(irqDevice_class);
        unregister_chrdev_region(irqDevice_majorminor, NUM_IRQS);
        return ret;
    }

    // 4. Criar ficheiros /dev/irqX
    for (i = 0; i < NUM_IRQS; i++) {
        dev_ret = device_create(irqDevice_class, NULL,
                                MKDEV(MAJOR(irqDevice_majorminor), i),
                                NULL, "irq%d", i);
        if (IS_ERR(dev_ret)) {
            while (i > 0) {
                i--;
                device_destroy(irqDevice_class, MKDEV(MAJOR(irqDevice_majorminor), i));
            }
            cdev_del(&c_dev);
            class_destroy(irqDevice_class);
            unregister_chrdev_region(irqDevice_majorminor, NUM_IRQS);
            return PTR_ERR(dev_ret);
        }
    }

    // 5. Mapear GPIO
    s_pGpioRegisters = (struct GpioRegisters *)ioremap(GPIO_BASE, sizeof(struct GpioRegisters));
    if (!s_pGpioRegisters) {
        for (i = 0; i < NUM_IRQS; i++) {
            device_destroy(irqDevice_class, MKDEV(MAJOR(irqDevice_majorminor), i));
        }
        cdev_del(&c_dev);
        class_destroy(irqDevice_class);
        unregister_chrdev_region(irqDevice_majorminor, NUM_IRQS);
        return -ENOMEM;
    }

    // 6. Configurar Interrupções
    for (i = 0; i < NUM_IRQS; i++) {
        struct SensorConfig *sensor = &my_sensors[i];
        int gpio_logical;

        SetGPIOFunction(s_pGpioRegisters, sensor->gpio_pin, 0);
        // PIR/reed/fingerprint -> pull-down (2); RFID -> pull-up (1)
        if (i != 4 && i != 5) {
            SetGPIOPullUpDown(s_pGpioRegisters, sensor->gpio_pin, 2);
        } else {
            SetGPIOPullUpDown(s_pGpioRegisters, sensor->gpio_pin, 1);
        }

        // Offset do chip GPIO no BCM2711: 512 + pino
        gpio_logical = 512 + sensor->gpio_pin;
        irq_numbers[i] = gpio_to_irq(gpio_logical);

        if (irq_numbers[i] < 0) {
            pr_err("irqModule: gpio_to_irq falhou no pino %d (ret=%d)\n",
                   sensor->gpio_pin, irq_numbers[i]);

            while (i > 0) {
                i--;
                free_irq(irq_numbers[i], (void *)&my_sensors[i]);
            }
            iounmap(s_pGpioRegisters);
            for (i = 0; i < NUM_IRQS; i++) {
                device_destroy(irqDevice_class, MKDEV(MAJOR(irqDevice_majorminor), i));
            }
            cdev_del(&c_dev);
            class_destroy(irqDevice_class);
            unregister_chrdev_region(irqDevice_majorminor, NUM_IRQS);
            return irq_numbers[i];
        }

        pr_alert("irqModule: %s (Pino %d) -> IRQ %d\n",
                 sensor->name, sensor->gpio_pin, irq_numbers[i]);

        // dev_id = configuração do sensor; recuperada no handler via casting de dev_id
        ret = request_irq(irq_numbers[i],
                          gpio_irq_handler,
                          sensor->trigger,
                          sensor->name,
                          (void *)sensor);

        if (ret) {
            pr_err("irqModule: request_irq falhou para %s (ret=%d)\n",
                   sensor->name, ret);

            while (i > 0) {
                i--;
                free_irq(irq_numbers[i], (void *)&my_sensors[i]);
            }
            iounmap(s_pGpioRegisters);
            for (i = 0; i < NUM_IRQS; i++) {
                device_destroy(irqDevice_class, MKDEV(MAJOR(irqDevice_majorminor), i));
            }
            cdev_del(&c_dev);
            class_destroy(irqDevice_class);
            unregister_chrdev_region(irqDevice_majorminor, NUM_IRQS);
            return ret;
        }

        sensor->enabled = true;
    }

    pr_alert("irqModule: Sucesso!\n");
    return 0;
}


static void __exit irqModule_exit(void) {
    int i;
    pr_alert("%s: called\n", __FUNCTION__);

    // 1. Libertar as Interrupções (dev_id tem de ser igual ao do request_irq)
    for (i = 0; i < NUM_IRQS; i++) {
        free_irq(irq_numbers[i], (void *)&my_sensors[i]);
        pr_alert("irqModule: IRQ %d libertada para o sensor %s\n",
                 irq_numbers[i], my_sensors[i].name);

        // Repor o pino como INPUT (0) ao sair
        SetGPIOFunction(s_pGpioRegisters, my_sensors[i].gpio_pin, 0);
    }

    // 2. Libertar memória virtual (IOUNMAP)
    if (s_pGpioRegisters) {
        iounmap(s_pGpioRegisters);
    }

    // 3. Destruir a estrutura CDEV
    cdev_del(&c_dev);

    // 4. Destruir os ficheiros /dev/irq0, /dev/irq1...
    for (i = 0; i < NUM_IRQS; i++) {
        device_destroy(irqDevice_class, MKDEV(MAJOR(irqDevice_majorminor), i));
    }

    // 5. Destruir a classe e libertar os números Major/Minor
    class_destroy(irqDevice_class);
    unregister_chrdev_region(irqDevice_majorminor, NUM_IRQS);

    pr_alert("%s: Driver descarregado com sucesso.\n", __FUNCTION__);
}

module_init(irqModule_init);
module_exit(irqModule_exit);
