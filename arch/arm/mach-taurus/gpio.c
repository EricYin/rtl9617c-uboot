// SPDX-License-Identifier: GPL-2.0+
/*
 * Realtek RTL9617C (Cortina "Taurus") GPIO.
 *
 * The controller has five banks of 32 pins. Each bank owns a pin mux, a
 * direction and an output register, and the mux bit has to be set before a pad
 * is readable or drivable as GPIO. Pins are addressed globally here so a board
 * never has to know about the banking.
 */
#include <config.h>
#include <asm/io.h>
#include <linux/kernel.h>
#include <asm/arch/registers.h>
#include <linux/errno.h>
#include <asm/arch/soc.h>

struct taurus_gpio_bank {
	unsigned long cfg;
	unsigned long out;
	unsigned long in;
	unsigned long mux;
};

static const struct taurus_gpio_bank taurus_gpio_banks[TAURUS_GPIO_BANKS] = {
	{ PER_GPIO0_CFG, PER_GPIO0_OUT, PER_GPIO0_IN, GLOBAL_GPIO_MUX_0 },
	{ PER_GPIO1_CFG, PER_GPIO1_OUT, PER_GPIO1_IN, GLOBAL_GPIO_MUX_1 },
	{ PER_GPIO2_CFG, PER_GPIO2_OUT, PER_GPIO2_IN, GLOBAL_GPIO_MUX_2 },
	{ PER_GPIO3_CFG, PER_GPIO3_OUT, PER_GPIO3_IN, GLOBAL_GPIO_MUX_3 },
	{ PER_GPIO4_CFG, PER_GPIO4_OUT, PER_GPIO4_IN, GLOBAL_GPIO_MUX_4 },
};

static const struct taurus_gpio_bank *bank_of(unsigned int pin)
{
	if (pin >= TAURUS_GPIO_PINS)
		return NULL;

	return &taurus_gpio_banks[pin / TAURUS_GPIO_BANK_PINS];
}

int taurus_gpio_group_select(struct taurus_gpio_group *group,
			     const unsigned int *pins, unsigned int count)
{
	unsigned int i;

	if (!count || count > TAURUS_GPIO_BANK_PINS || !bank_of(pins[0]))
		return -EINVAL;

	group->bank = pins[0] / TAURUS_GPIO_BANK_PINS;
	group->mask = 0;
	for (i = 0; i < count; i++) {
		/* A group is driven in one register write, so one bank only. */
		if (pins[i] >= TAURUS_GPIO_PINS ||
		    pins[i] / TAURUS_GPIO_BANK_PINS != group->bank)
			return -EINVAL;
		group->mask |= BIT(pins[i] % TAURUS_GPIO_BANK_PINS);
	}

	return 0;
}

void taurus_gpio_group_claim(struct taurus_gpio_group *group, u32 values)
{
	const struct taurus_gpio_bank *bank = &taurus_gpio_banks[group->bank];

	group->mux = readl(bank->mux);
	group->cfg = readl(bank->cfg);
	group->out = readl(bank->out);

	setbits_le32(bank->mux, group->mask);
	clrsetbits_le32(bank->out, group->mask, values & group->mask);
	clrbits_le32(bank->cfg, group->mask);
}

void taurus_gpio_group_write(struct taurus_gpio_group *group, u32 values)
{
	const struct taurus_gpio_bank *bank = &taurus_gpio_banks[group->bank];

	clrsetbits_le32(bank->out, group->mask, values & group->mask);
}

void taurus_gpio_group_release(struct taurus_gpio_group *group)
{
	const struct taurus_gpio_bank *bank = &taurus_gpio_banks[group->bank];

	clrsetbits_le32(bank->out, group->mask, group->out & group->mask);
	clrsetbits_le32(bank->cfg, group->mask, group->cfg & group->mask);
	clrsetbits_le32(bank->mux, group->mask, group->mux & group->mask);
}

void taurus_gpio_input(unsigned int pin)
{
	const struct taurus_gpio_bank *bank = bank_of(pin);

	if (!bank)
		return;
	setbits_le32(bank->mux, BIT(pin % TAURUS_GPIO_BANK_PINS));
	setbits_le32(bank->cfg, BIT(pin % TAURUS_GPIO_BANK_PINS));
}

void taurus_gpio_output(unsigned int pin, bool value)
{
	const struct taurus_gpio_bank *bank = bank_of(pin);
	u32 bit;

	if (!bank)
		return;
	bit = BIT(pin % TAURUS_GPIO_BANK_PINS);
	setbits_le32(bank->mux, bit);
	clrsetbits_le32(bank->out, bit, value ? bit : 0);
	clrbits_le32(bank->cfg, bit);
}

void taurus_gpio_set(unsigned int pin, bool value)
{
	const struct taurus_gpio_bank *bank = bank_of(pin);
	u32 bit;

	if (!bank)
		return;
	bit = BIT(pin % TAURUS_GPIO_BANK_PINS);
	clrsetbits_le32(bank->out, bit, value ? bit : 0);
}

bool taurus_gpio_get(unsigned int pin)
{
	const struct taurus_gpio_bank *bank = bank_of(pin);

	if (!bank)
		return false;

	return readl(bank->in) & BIT(pin % TAURUS_GPIO_BANK_PINS);
}
