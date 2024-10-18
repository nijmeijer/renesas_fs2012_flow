// SPDX-License-Identifier: GPL-2.0
/*
 * Renesas FS2012 Flow sensor driver.
 *
 * Copyright (C) 2024 Alex Nijmeijer
 *
 * List of features not yet supported by the driver:
 * - n.a.
 */

#include <linux/bitops.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/regmap.h>
#include <linux/time64.h>

#include <linux/iio/iio.h>

#define DRIVER_NAME "renesas_fs2012_flow"

struct RENESAS_FS2012_dev {
	struct i2c_client *client;
	/* Protects access to IIO attributes. */
	struct mutex lock;
};

/* Custom  read/write operations: perform unlocked access to the i2c bus. */
static int RENESAS_FS2012_read_word(struct RENESAS_FS2012_dev *RENESAS_FS2012, u16 *val)
{
	const struct i2c_client *client = RENESAS_FS2012->client;
	const struct device *dev = &client->dev;
	__be16 be_val;
	int ret;
        char buf[5];
        struct i2c_msg msg_resp = {
		.addr = client->addr,
		.flags =  I2C_M_RD,
		.len = sizeof(be_val),
		.buf = (__u8 *) &buf,
	};
	i2c_lock_bus(client->adapter, I2C_LOCK_SEGMENT);

        ret = __i2c_transfer(client->adapter, &msg_resp, 1);

	i2c_unlock_bus(client->adapter, I2C_LOCK_SEGMENT);
	if (ret<0) {
		dev_err(dev, "Read word failed: (%d)\n",  ret);
		return ret;
	}
        memcpy(&be_val, &buf[0], sizeof(be_val));
	*val = be16_to_cpu(be_val);

	return 0;
}

static const struct iio_chan_spec RENESAS_FS2012_channels[] = {
	{
		.type = IIO_VELOCITY,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SCALE),
	},
};

static int RENESAS_FS2012_read_raw(struct iio_dev *iio_dev,
			    const struct iio_chan_spec *chan,
			    int *val, int *val2, long mask)
{
	struct RENESAS_FS2012_dev *RENESAS_FS2012 = iio_priv(iio_dev);
	u16 value;
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		switch (chan->type) {
		case IIO_VELOCITY:
			mutex_lock(&RENESAS_FS2012->lock);
			ret = RENESAS_FS2012_read_word(RENESAS_FS2012, &value);
			mutex_unlock(&RENESAS_FS2012->lock);

			if (ret)
				return ret;

			*val = value;
			return IIO_VAL_INT;

		default:
			return -EINVAL;
		}

	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_VELOCITY:
			/*
			 * Gas Part Configurations    (-NG ending for part code number: divide by 1000 for Liters / minute (SLPM)
                         * Liquid Part Configurations (-LQ ending for part code number: divide by 10 for Liters / minute (SCCM)
			 */
			*val = 1;
			*val2 = 1000;
			return IIO_VAL_FRACTIONAL;

		default:
			return -EINVAL;
		}

	default:
		return -EINVAL;
	}
}

static const struct iio_info RENESAS_FS2012_info = {
	.read_raw = RENESAS_FS2012_read_raw,
};

static int RENESAS_FS2012_probe(struct i2c_client *client)
{
	struct RENESAS_FS2012_dev *RENESAS_FS2012;
	struct iio_dev *iio_dev;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA |
						      I2C_FUNC_SMBUS_BLOCK_DATA)) {
		dev_err(&client->dev,
			"Adapter does not support required functionalities\n");
		return -EOPNOTSUPP;
	}

	iio_dev = devm_iio_device_alloc(&client->dev, sizeof(*RENESAS_FS2012));
	if (!iio_dev)
		return -ENOMEM;

	RENESAS_FS2012 = iio_priv(iio_dev);
	RENESAS_FS2012->client = client;
	mutex_init(&RENESAS_FS2012->lock);

	i2c_set_clientdata(client, RENESAS_FS2012);

	iio_dev->info = &RENESAS_FS2012_info;
	iio_dev->name = DRIVER_NAME;
	iio_dev->channels = RENESAS_FS2012_channels;
	iio_dev->num_channels = ARRAY_SIZE(RENESAS_FS2012_channels);
	iio_dev->modes = INDIO_DIRECT_MODE;

        mutex_unlock(&RENESAS_FS2012->lock);

	return devm_iio_device_register(&client->dev, iio_dev);
}

static const struct of_device_id RENESAS_FS2012_of_match[] = {
	{ .compatible = "renesas,renesas-fs2012-flow" },
	{}
};
MODULE_DEVICE_TABLE(of, RENESAS_FS2012_of_match);

static struct i2c_driver RENESAS_FS2012_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = RENESAS_FS2012_of_match,
	},
	.probe = RENESAS_FS2012_probe,
};
module_i2c_driver(RENESAS_FS2012_driver);

MODULE_AUTHOR("Alex Nijmeijer <alex.nijmeijer@neads.nl>");
MODULE_DESCRIPTION("Renesas FS2012 Flow sensor IIO driver");
MODULE_LICENSE("GPL v2");
