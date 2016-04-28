/*
 * Copyright (C) 2012 Avionic Design GmbH
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/module.h>

#include <linux/iio/iio.h>
#include <linux/regulator/consumer.h>

//#include <linux/ti-adc081c.h>

struct adc081c {
	struct i2c_client *i2c;
	struct regulator *ref;
};

struct iio_dev *m_iio; 

#define REG_CONV_RES 0x00
#define REG_ALT_STAT 0x01
#define REG_CONF_REG 0x02
#define REG_LOW_ALERT 0x03
#define REG_HIGH_ALERT 0x04
#define REG_HYST_ALERT 0x05
#define REG_LOW_CONV 0x06
#define REG_HIGH_CONV 0x07

static ssize_t adc081c_reg_show(struct device *dev,
        struct device_attribute *attr, char *buf)
{
	int i, err, value;
	struct adc081c *adc = iio_priv(m_iio);

	err = i2c_smbus_read_word_swapped(adc->i2c, REG_CONV_RES);
	if (err < 0)
		return err;

	value = (err >> 4) & 0xff;
	sprintf(buf, "Conversation Result %x\n", value);
	printk("%s : Conversation Result %x\n", __func__, value);
	
	err = i2c_smbus_read_word_swapped(adc->i2c, REG_LOW_ALERT);
	if (err < 0)
		return err;

	value = (err >> 4) & 0xff;
	sprintf(buf, "V_low Alert %x\n", value);
	printk("%s : V_low Alert %x\n", __func__, value);
	
	err = i2c_smbus_read_word_swapped(adc->i2c, REG_HIGH_ALERT);
	if (err < 0)
		return err;

	value = (err >> 4) & 0xff;
	sprintf(buf, "V_high Alert %x\n", value);
	printk("%s : V_high Alert %x\n", __func__, value);
	return sprintf(buf, "\n");
}

static ssize_t adc081c_reg_store(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t count)
{
	return 0;
}
static DEVICE_ATTR(adc081c_reg, 0644, adc081c_reg_show, adc081c_reg_store);

static int adc081c_read_raw(struct iio_dev *iio,
			    struct iio_chan_spec const *channel, int *value,
			    int *shift, long mask)
{
	struct adc081c *adc = iio_priv(iio);
	int err;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		err = i2c_smbus_read_word_swapped(adc->i2c, REG_CONV_RES);
		if (err < 0)
			return err;

		*value = (err >> 4) & 0xff;
		return IIO_VAL_INT;

	case IIO_CHAN_INFO_SCALE:
		return 0;
		/*
		err = regulator_get_voltage(adc->ref);
		if (err < 0)
			return err;

		*value = err / 1000;
		*shift = 8;

		return IIO_VAL_FRACTIONAL_LOG2;
		*/
	default:
		break;
	}

	return -EINVAL;
}

void get_adc081c_value(int *value)
{
	if (m_iio == NULL) {
		printk("%s() : m_iio null pointer\n", __func__);
		return;
	}
	struct adc081c *adc = iio_priv(m_iio);
	int err;
	
	if (adc == NULL) {
		printk("%s() : adc null pointer\n", __func__);
		return;
	}
	err = i2c_smbus_read_word_swapped(adc->i2c, REG_CONV_RES);
	if (err < 0) {
		printk("%s() : error %d\n", __func__, err);
		return;
	}

	*value = (err >> 4) & 0xff;
	pr_info("%s() : value %x\n", __func__, *value);
	return;
}
EXPORT_SYMBOL(get_adc081c_value);

static const struct iio_chan_spec adc081c_channel = {
	.type = IIO_VOLTAGE,
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
};

static const struct iio_info adc081c_info = {
	.read_raw = adc081c_read_raw,
	.driver_module = THIS_MODULE,
};

static int adc081c_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct iio_dev *iio;
	struct adc081c *adc;
	int err;
	
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_WORD_DATA))
		return -ENODEV;
	
	iio = iio_device_alloc(sizeof(*adc));
	if (!iio)
		return -ENOMEM;
	
	
	adc = iio_priv(iio);
	adc->i2c = client;
	/*	
	adc->ref = regulator_get(&client->dev, "vref");
	if (IS_ERR(adc->ref)) {
		err = PTR_ERR(adc->ref);
		goto iio_free;
	}

	err = regulator_enable(adc->ref);
	if (err < 0)
		goto regulator_put;
	*/

	iio->dev.parent = &client->dev;
	iio->name = dev_name(&client->dev);
	iio->modes = INDIO_DIRECT_MODE;
	iio->info = &adc081c_info;

	iio->channels = &adc081c_channel;
	iio->num_channels = 1;

	err = iio_device_register(iio);
	if (err < 0)
		goto regulator_disable;

	i2c_set_clientdata(client, iio);

	m_iio = iio;
	
	err = device_create_file(iio->dev.parent, &dev_attr_adc081c_reg);
	if (err != 0) {
		dev_err(iio->dev.parent,
				"Failed to crate adc081c_reg %d\n", err);
		return err;
	}

	printk("%s() : probe success\n", __func__);

	return 0;

regulator_disable:
	regulator_disable(adc->ref);
regulator_put:
	regulator_put(adc->ref);
iio_free:
	iio_device_free(iio);

	return err;
}

static int adc081c_remove(struct i2c_client *client)
{
	struct iio_dev *iio = i2c_get_clientdata(client);
	struct adc081c *adc = iio_priv(iio);

	iio_device_unregister(iio);
	regulator_disable(adc->ref);
	regulator_put(adc->ref);
	iio_device_free(iio);

	return 0;
}

static const struct i2c_device_id adc081c_id[] = {
	{ "adc081c", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, adc081c_id);

#ifdef CONFIG_OF
static const struct of_device_id adc081c_of_match[] = {
	{ .compatible = "ti,adc081c" },
	{ }
};
MODULE_DEVICE_TABLE(of, adc081c_of_match);
#endif

static struct i2c_driver adc081c_driver = {
	.driver = {
		.name = "adc081c",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(adc081c_of_match),
	},
	.probe = adc081c_probe,
	.remove = adc081c_remove,
	.id_table = adc081c_id,
};
module_i2c_driver(adc081c_driver);

MODULE_AUTHOR("Thierry Reding <thierry.reding@avionic-design.de>");
MODULE_DESCRIPTION("Texas Instruments ADC081C021/027 driver");
MODULE_LICENSE("GPL v2");
