/*
 * ALSA Driver for MCP3002 ADC
 */

#include <linux/err.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/timer.h>

#include <sound/initval.h>
#include <sound/control.h>
#include <sound/core.h>
#include <sound/pcm.h>

#include <linux/spi/spi.h>

#define BITRATE_MIN	 8000 /* Hardware limit? */
#define BITRATE_TARGET	8000
#define BITRATE_MAX	50000 /* Hardware limit. */

struct snd_mcp3002 {
	struct snd_card			*card;
	struct snd_pcm			*pcm;
	struct snd_pcm_substream	*substream;
	int				period;	
	unsigned long			bitrate;
	struct spi_device		*spi;
	struct timer_list 		timer;
	u8				spi_wbuffer[2];
	u8				spi_rbuffer[2];
	spinlock_t			lock;
};

static int snd_mcp3002_write_reg(struct snd_mcp3002 *chip, u8 reg, u8 val)
{
	struct spi_message msg;
	struct spi_transfer msg_xfer = {
		.len		= 2,
		.cs_change	= 0,
	};
	int retval;

	spi_message_init(&msg);

	chip->spi_wbuffer[0] = reg;
	chip->spi_wbuffer[1] = val;

	msg_xfer.tx_buf = chip->spi_wbuffer;
	msg_xfer.rx_buf = chip->spi_rbuffer;
	spi_message_add_tail(&msg_xfer, &msg);

	retval = spi_sync(chip->spi, &msg);

	return retval;
}

static struct snd_pcm_hardware snd_mcp3002_playback_hw = {
	.info		= SNDRV_PCM_INFO_INTERLEAVED |
			  SNDRV_PCM_INFO_BLOCK_TRANSFER,
	.formats	= SNDRV_PCM_FMTBIT_S16_BE,
	.rates		= SNDRV_PCM_RATE_CONTINUOUS,
	.rate_min	= 8000,  /* Replaced by chip->bitrate later. */
	.rate_max	= 50000, /* Replaced by chip->bitrate later. */
	.channels_min	= 1,
	.channels_max	= 2,
	.buffer_bytes_max = 64 * 1024 - 1,
	.period_bytes_min = 512,
	.period_bytes_max = 64 * 1024 - 1,
	.periods_min	= 4,
	.periods_max	= 1024,
};

/*
 * Calculate and set bitrate and divisions.
 */
static int snd_mcp3002_set_bitrate(struct snd_mcp3002 *chip)
{
	unsigned long ssc_rate = 8000;
	unsigned long ssc_div;
	unsigned long ssc_div_max, ssc_div_min;

	/* SSC clock / (bitrate * stereo * 16-bit). */
	ssc_div = ssc_rate / (BITRATE_TARGET * 2 * 16);
	ssc_div_min = ssc_rate / (BITRATE_MAX * 2 * 16);
	ssc_div_max = ssc_rate / (BITRATE_MIN * 2 * 16);

	/* ssc_div must be even. */
	ssc_div = (ssc_div + 1) & ~1UL;

	if ((ssc_rate / (ssc_div * 2 * 16)) < BITRATE_MIN) {
		ssc_div -= 2;
		if ((ssc_rate / (ssc_div * 2 * 16)) > BITRATE_MAX)
			return -ENXIO;
	}

	/* SSC clock / (ssc divider * 16-bit * stereo). */
	chip->bitrate = ssc_rate / (ssc_div * 16 * 2);

	dev_info(&chip->spi->dev,
			"mcp3002: supported bitrate is %lu (%lu divider)\n",
			chip->bitrate, ssc_div);

	return 0;
}

// =======================
// PCM callbacks
// =======================
static int snd_mcp3002_pcm_open(struct snd_pcm_substream *substream)
{
	struct snd_mcp3002 *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	int err;

	/* ensure buffer_size is a multiple of period_size */
	err = snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS);
	if (err < 0)
		return err;
	snd_mcp3002_playback_hw.rate_min = chip->bitrate;
	snd_mcp3002_playback_hw.rate_max = chip->bitrate;
	runtime->hw = snd_mcp3002_playback_hw;
	chip->substream = substream;

	return 0;
}

static int snd_mcp3002_pcm_close(struct snd_pcm_substream *substream)
{
	struct snd_mcp3002 *chip = snd_pcm_substream_chip(substream);
	chip->substream = NULL;
	return 0;
}

static int snd_mcp3002_pcm_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *hw_params)
{
	return snd_pcm_lib_malloc_pages(substream,params_buffer_bytes(hw_params));
}

static int snd_mcp3002_pcm_hw_free(struct snd_pcm_substream *substream)
{
	return snd_pcm_lib_free_pages(substream);
}

static int snd_mcp3002_pcm_prepare(struct snd_pcm_substream *substream)
{
	struct snd_mcp3002 *chip = snd_pcm_substream_chip(substream);
	chip->period = 0;
	
	return 0;
}

static int snd_mcp3002_pcm_trigger(struct snd_pcm_substream *substream,
				   int cmd)
{
	struct snd_mcp3002 *chip = snd_pcm_substream_chip(substream);
	int retval = 0;

	spin_lock(&chip->lock);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		break;
	default:
		dev_dbg(&chip->spi->dev, "spurious command %x\n", cmd);
		retval = -EINVAL;
		break;
	}
 
	spin_unlock(&chip->lock);

	return retval;
}

static snd_pcm_uframes_t snd_mcp3002_pcm_pointer(struct snd_pcm_substream *substream)
{
	struct snd_mcp3002 *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	snd_pcm_uframes_t pos;
	unsigned long bytes = 0;

	pos = bytes_to_frames(runtime, bytes);
	if (pos >= runtime->buffer_size)
		pos -= runtime->buffer_size;

	return pos;
}

static struct snd_pcm_ops snd_mcp3002_capture_ops = {
	.open		= snd_mcp3002_pcm_open,
	.close		= snd_mcp3002_pcm_close,
	.ioctl		= snd_pcm_lib_ioctl,
	.hw_params	= snd_mcp3002_pcm_hw_params,
	.hw_free	= snd_mcp3002_pcm_hw_free,
	.prepare	= snd_mcp3002_pcm_prepare,
	.trigger	= snd_mcp3002_pcm_trigger,
	.pointer	= snd_mcp3002_pcm_pointer,
};
// =======================

// =======================
// timer callback
// =======================
static void my_timer_callback( unsigned long data )
{
	struct snd_mcp3002 *chip = (struct snd_mcp3002 *)data;
	struct snd_pcm_runtime *runtime = chip->substream->runtime;
	int offset;
	int block_size;
	int next_period;
	int retval;
	u16 value;

	printk("Timer callback\n");
	
	spin_lock(&chip->lock);

	block_size = frames_to_bytes(runtime, runtime->period_size);
	
	retval = snd_mcp3002_write_reg(chip, 0xD0,0);
	if (!retval)
	{
		chip->period++;
		if (chip->period == runtime->periods)
			chip->period = 0;
		next_period = chip->period + 1;
		if (next_period == runtime->periods)
			next_period = 0;

		offset = block_size * next_period;
		
		value = ( (chip->spi_rbuffer[0]*128) | (chip->spi_rbuffer[1]>>1) );
	}
	
	spin_unlock(&chip->lock);

	snd_pcm_period_elapsed(chip->substream);

	retval = mod_timer( &chip->timer, jiffies + msecs_to_jiffies(200) );
	if (retval) 
		printk("Error in mod_timer\n");	
}

static int snd_mcp3002_pcm_new(struct snd_mcp3002 *chip, int device)
{
	struct snd_pcm *pcm;
	int retval;

	retval = snd_pcm_new(chip->card, chip->card->shortname, device, 0, 1, &pcm);
	if (retval < 0)
		goto out;

	pcm->private_data = chip;
	pcm->info_flags = SNDRV_PCM_INFO_BLOCK_TRANSFER;
	strcpy(pcm->name, "mcp3002");
	chip->pcm = pcm;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_mcp3002_capture_ops);

	retval = snd_pcm_lib_preallocate_pages_for_all(chip->pcm, SNDRV_DMA_TYPE_DEV, NULL, 64 * 1024, 64 * 1024);
out:
	return retval;
}

static int snd_mcp3002_chip_init(struct snd_mcp3002 *chip)
{
	int retval;

	retval = snd_mcp3002_set_bitrate(chip);
	if (retval)
	{
		printk("snd_mcp3002_set_bitrate failed:%d\n", retval);
		goto out;
	}

	setup_timer( &chip->timer, my_timer_callback, (unsigned long)chip );

	printk( "Starting timer to fire in 200ms (%ld)\n", jiffies );
	retval = mod_timer( &chip->timer, jiffies + msecs_to_jiffies(200) );
	if (retval) 
	{
		printk("Error in mod_timer\n");
	}
	
	goto out;

out:
	return retval;
}

static int snd_mcp3002_dev_free(struct snd_device *device)
{
	struct snd_mcp3002 *chip = device->device_data;
	int retval = del_timer( &chip->timer );
	
	if (retval) 
	{
		printk("The timer is still in use...\n");
	}
	
	return 0;
}

static int snd_mcp3002_dev_init(struct snd_card *card,
					 struct spi_device *spi)
{
	static struct snd_device_ops ops = {
		.dev_free	= snd_mcp3002_dev_free,
	};
	struct snd_mcp3002 *chip = card->private_data;
	int retval;

	spin_lock_init(&chip->lock);
	chip->card = card;
	
	retval = snd_mcp3002_chip_init(chip);
	if (retval)
	{
		printk("snd_mcp3002_chip_init failed:%d\n", retval);
		goto out;
	}

	retval = snd_mcp3002_pcm_new(chip, 0);
	if (retval)
	{
		printk("snd_mcp3002_pcm_new failed:%d\n", retval);
		goto out;
	}

	retval = snd_device_new(card, SNDRV_DEV_LOWLEVEL, chip, &ops);
	if (retval)
	{
		printk("snd_device_new failed:%d\n", retval);
		goto out;
	}

	snd_card_set_dev(card, &spi->dev);
out:

	return retval;
}

static int snd_mcp3002_probe(struct spi_device *spi)
{	
	struct snd_card			*card;
	struct snd_mcp3002		*chip;
	int				retval;
	char				id[16];


	printk("snd_mcp3002_probe\n");
	
	// alsa card initialization
	snprintf(id, sizeof id, KBUILD_MODNAME);
	retval = snd_card_create(-1, id, THIS_MODULE, sizeof(struct snd_mcp3002), &card);
	if (retval < 0)
	{
		printk("snd_card_create failed:%d\n", retval);
		goto out;
	}

	strcpy(card->driver, KBUILD_MODNAME);
	strcpy(card->shortname, KBUILD_MODNAME);
	strcpy(card->longname, KBUILD_MODNAME);

	retval = snd_card_register(card);
	if (retval)
	{
		printk("snd_card_register failed:%d\n", retval);
		goto out_card;
	}

	// spi initialization
	chip = card->private_data;
	chip->spi = spi;

	retval = snd_mcp3002_dev_init(card, spi);
	if (retval) 
	{
		printk("snd_mcp3002_dev_init failed:%d\n", retval);
		goto out_card;
	}
		
	dev_set_drvdata(&spi->dev, card);

	goto out;

out_card:
	snd_card_free(card);
out:
	return retval;
}

static int snd_mcp3002_remove(struct spi_device *spi)
{
	struct snd_card *card = dev_get_drvdata(&spi->dev);
	struct snd_mcp3002 *chip = card->private_data;
	int retval = 0;

	printk("snd_mcp3002_remove\n");
	
	snd_card_free(card);
	dev_set_drvdata(&spi->dev, NULL);

	return retval;
}

static struct spi_driver mcp3002_driver = {
	.driver		= { 
		.name	= "spidev", 
		.owner  = THIS_MODULE
	},
	.probe		= snd_mcp3002_probe,
	.remove		= snd_mcp3002_remove,
};

static int __init mcp3002_init(void)
{
	int ret;
	
	printk("mcp3002_init\n");
	ret = spi_register_driver(&mcp3002_driver);
	if (ret <0 )
	{
		printk("spi_register_driver fails :%d\n",ret);
	}
	return ret;
}
module_init(mcp3002_init);

static void __exit mcp3002_exit(void)
{
	printk("mcp3002_exit\n");
	spi_unregister_driver(&mcp3002_driver);
}
module_exit(mcp3002_exit);

MODULE_AUTHOR("MPR");
MODULE_DESCRIPTION("Sound driver for MCP3002");
MODULE_LICENSE("GPL");
MODULE_ALIAS("spi:" KBUILD_MODNAME);
