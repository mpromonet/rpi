/*
 * MCP23008 I2C/GPIO gpio expander driver
 */
#define DEBUG

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <asm/byteorder.h>

static long int p_base = 0;
module_param (p_base, long,S_IRUGO);

/**
 * MCP types supported by driver
 */
#define MCP_TYPE_008	0

/* Registers are all 8 bits wide.
 *
 * The mcp23s17 has twice as many bits, and can be configured to work
 * with either 16 bit registers or with two adjacent 8 bit banks.
 */
#define MCP_IODIR	0x00		/* init/reset:  all ones */
#define MCP_IPOL	0x01
#define MCP_GPINTEN	0x02
#define MCP_DEFVAL	0x03
#define MCP_INTCON	0x04
#define MCP_IOCON	0x05
#	define IOCON_SEQOP	(1 << 5)
#	define IOCON_HAEN	(1 << 3)
#	define IOCON_ODR	(1 << 2)
#	define IOCON_INTPOL	(1 << 1)
#define MCP_GPPU	0x06
#define MCP_INTF	0x07
#define MCP_INTCAP	0x08
#define MCP_GPIO	0x09
#define MCP_OLAT	0x0a

struct mcp23s08;

struct mcp23s08_ops {
	int	(*read)(struct mcp23s08 *mcp, unsigned reg);
	int	(*write)(struct mcp23s08 *mcp, unsigned reg, unsigned val);
	int	(*read_regs)(struct mcp23s08 *mcp, unsigned reg,
			     u16 *vals, unsigned n);
};

struct mcp23s08 {
	u8			addr;

	u16			cache[11];
	/* lock protects the cached values */
	struct mutex		lock;

	struct gpio_chip	chip;

	const struct mcp23s08_ops	*ops;
	void			*data; /* ops specific data */
};


static int mcp23008_read(struct mcp23s08 *mcp, unsigned reg)
{
	return i2c_smbus_read_byte_data(mcp->data, reg);
}

static int mcp23008_write(struct mcp23s08 *mcp, unsigned reg, unsigned val)
{
	return i2c_smbus_write_byte_data(mcp->data, reg, val);
}

static int mcp23008_read_regs(struct mcp23s08 *mcp, unsigned reg, u16 *vals, unsigned n)
{
	while (n--) {
		int ret = mcp23008_read(mcp, reg++);
		if (ret < 0)
			return ret;
		*vals++ = ret;
	}

	return 0;
}

static const struct mcp23s08_ops mcp23008_ops = {
	.read		= mcp23008_read,
	.write		= mcp23008_write,
	.read_regs	= mcp23008_read_regs,
};

static int mcp23s08_direction_input(struct gpio_chip *chip, unsigned offset)
{
	struct mcp23s08	*mcp = container_of(chip, struct mcp23s08, chip);
	int status;

	mutex_lock(&mcp->lock);
	mcp->cache[MCP_IODIR] |= (1 << offset);
	status = mcp->ops->write(mcp, MCP_IODIR, mcp->cache[MCP_IODIR]);
	mutex_unlock(&mcp->lock);
	return status;
}

static int mcp23s08_get(struct gpio_chip *chip, unsigned offset)
{
	struct mcp23s08	*mcp = container_of(chip, struct mcp23s08, chip);
	int status;

	mutex_lock(&mcp->lock);

	/* REVISIT reading this clears any IRQ ... */
	status = mcp->ops->read(mcp, MCP_GPIO);
	if (status < 0)
	{
		status = 0;
	}
	else 
	{
		mcp->cache[MCP_GPIO] = status;
		status = !!(status & (1 << offset));
	}
	mutex_unlock(&mcp->lock);
	return status;
}

static int __mcp23s08_set(struct mcp23s08 *mcp, unsigned mask, int value)
{
	unsigned olat = mcp->cache[MCP_OLAT];

	if (value)
		olat |= mask;
	else
		olat &= ~mask;
	mcp->cache[MCP_OLAT] = olat;
	return mcp->ops->write(mcp, MCP_OLAT, olat);
}

static void mcp23s08_set(struct gpio_chip *chip, unsigned offset, int value)
{
	struct mcp23s08	*mcp = container_of(chip, struct mcp23s08, chip);
	unsigned mask = 1 << offset;

	mutex_lock(&mcp->lock);
	__mcp23s08_set(mcp, mask, value);
	mutex_unlock(&mcp->lock);
}

static int mcp23s08_direction_output(struct gpio_chip *chip, unsigned offset, int value)
{
	struct mcp23s08	*mcp = container_of(chip, struct mcp23s08, chip);
	unsigned mask = 1 << offset;
	int status;

	mutex_lock(&mcp->lock);
	status = __mcp23s08_set(mcp, mask, value);
	if (status == 0) {
		mcp->cache[MCP_IODIR] &= ~mask;
		status = mcp->ops->write(mcp, MCP_IODIR, mcp->cache[MCP_IODIR]);
	}
	mutex_unlock(&mcp->lock);
	return status;
}

/*----------------------------------------------------------------------*/

static int mcp23s08_probe_one(struct mcp23s08 *mcp, struct device *dev,
			      void *data, unsigned addr,
			      unsigned type, unsigned base, unsigned pullups)
{
	int status;

	mutex_init(&mcp->lock);

	mcp->data = data;
	mcp->addr = addr;

	mcp->chip.direction_input = mcp23s08_direction_input;
	mcp->chip.get = mcp23s08_get;
	mcp->chip.direction_output = mcp23s08_direction_output;
	mcp->chip.set = mcp23s08_set;
	mcp->chip.dbg_show = NULL;

	switch (type) {

	case MCP_TYPE_008:
		mcp->ops = &mcp23008_ops;
		mcp->chip.ngpio = 8;
		mcp->chip.label = "mcp23008";
		break;

	default:
		dev_err(dev, "invalid device type (%d)\n", type);
		return -EINVAL;
	}

	mcp->chip.base = base;
	mcp->chip.can_sleep = 1;
	mcp->chip.dev = dev;
	mcp->chip.owner = THIS_MODULE;

	/* verify MCP_IOCON.SEQOP = 0, so sequential reads work,
	 * and MCP_IOCON.HAEN = 1, so we work with all chips.
	 */
	status = mcp->ops->read(mcp, MCP_IOCON);
	if (status < 0)
		goto fail;
	if ((status & IOCON_SEQOP) || !(status & IOCON_HAEN)) {
		/* mcp23s17 has IOCON twice, make sure they are in sync */
		status &= ~(IOCON_SEQOP | (IOCON_SEQOP << 8));
		status |= IOCON_HAEN | (IOCON_HAEN << 8);
		status = mcp->ops->write(mcp, MCP_IOCON, status);
		if (status < 0)
			goto fail;
	}

	/* configure ~100K pullups */
	status = mcp->ops->write(mcp, MCP_GPPU, pullups);
	if (status < 0)
		goto fail;

	status = mcp->ops->read_regs(mcp, 0, mcp->cache, ARRAY_SIZE(mcp->cache));
	if (status < 0)
		goto fail;

	/* disable inverter on input */
	if (mcp->cache[MCP_IPOL] != 0) {
		mcp->cache[MCP_IPOL] = 0;
		status = mcp->ops->write(mcp, MCP_IPOL, 0);
		if (status < 0)
			goto fail;
	}

	/* disable irqs */
	if (mcp->cache[MCP_GPINTEN] != 0) {
		mcp->cache[MCP_GPINTEN] = 0;
		status = mcp->ops->write(mcp, MCP_GPINTEN, 0);
		if (status < 0)
			goto fail;
	}

	status = gpiochip_add(&mcp->chip);
fail:
	if (status < 0)
		dev_dbg(dev, "can't setup chip %d, --> %d\n",
			addr, status);
	return status;
}

/*----------------------------------------------------------------------*/


static int __devinit mcp230xx_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct mcp23s08 *mcp;
	int status;

	mcp = kzalloc(sizeof *mcp, GFP_KERNEL);
	if (!mcp)
		return -ENOMEM;

	i2c_set_clientdata(client, mcp);

	status = mcp23s08_probe_one(mcp, &client->dev, client, client->addr,
				    id->driver_data, p_base,   0xFF);
	if (status)
		goto fail;


	return 0;

fail:
	kfree(mcp);

	return status;
}

static int __devexit mcp230xx_remove(struct i2c_client *client)
{
	struct mcp23s08 *mcp = i2c_get_clientdata(client);
	int status;

	status = gpiochip_remove(&mcp->chip);
	if (status == 0)
		kfree(mcp);

	return status;
}

static const struct i2c_device_id mcp230xx_id[] = {
	{ "mcp23008", MCP_TYPE_008 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, mcp230xx_id);

static struct i2c_driver mcp230xx_driver = {
	.driver = {
		.name	= "mcp230xx",
		.owner	= THIS_MODULE,
	},
	.probe		= mcp230xx_probe,
	.remove		= __devexit_p(mcp230xx_remove),
	.id_table	= mcp230xx_id,
};

static int __init mcp23s08_i2c_init(void)
{
	return i2c_add_driver(&mcp230xx_driver);
}

static void mcp23s08_i2c_exit(void)
{
	i2c_del_driver(&mcp230xx_driver);
}


static int __init mcp23s08_init(void)
{
	int ret;

	ret = mcp23s08_i2c_init();
	if (ret)
		goto i2c_fail;

	return 0;

 i2c_fail:
	return ret;
}
/* register after i2c postcore initcall and before
 * subsys initcalls that may rely on these GPIOs
 */
subsys_initcall(mcp23s08_init);

static void __exit mcp23s08_exit(void)
{
	mcp23s08_i2c_exit();
}
module_exit(mcp23s08_exit);

MODULE_LICENSE("GPL");
