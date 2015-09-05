/*******************
  ALSA I2C PCF8591 
********************/

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/mutex.h>
#include <linux/err.h>
#include <linux/sound.h>
#include <linux/soundcard.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <linux/workqueue.h>

#include <asm/io.h>

#include <sound/core.h>
#include <sound/initval.h>
#include <sound/i2c.h>
#include <sound/pcm.h>

/* Insmod parameters */
static int input_mode;
module_param(input_mode, int, 0);
MODULE_PARM_DESC(input_mode,
        "Analog input mode:\n"
        " 0 = four single ended inputs\n"
        " 1 = three differential inputs\n"
        " 2 = single ended and differential mixed\n"
        " 3 = two differential inputs\n");

static int index = SNDRV_DEFAULT_IDX1; 
module_param(index, int, 0444);
MODULE_PARM_DESC(index, "Index value for soundcard.");
 
static char *id = KBUILD_MODNAME; 
module_param(id, charp, 0444);
MODULE_PARM_DESC(id, "ID string for soundcard.");

/*
 * The PCF8591 control byte
 *      7    6    5    4    3    2    1    0
 *   |  0 |AOEF|   AIP   |  0 |AINC|  AICH   |
 */

/* Analog Output Enable Flag (analog output active if 1) */
#define PCF8591_CONTROL_AOEF            0x40

/*
 * Analog Input Programming
 * 0x00 = four single ended inputs
 * 0x10 = three differential inputs
 * 0x20 = single ended and differential mixed
 * 0x30 = two differential inputs
 */
#define PCF8591_CONTROL_AIP_MASK        0x30

/* Autoincrement Flag (switch on if 1) */
#define PCF8591_CONTROL_AINC            0x04

/*
 * Channel selection
 * 0x00 = channel 0
 * 0x01 = channel 1
 * 0x02 = channel 2
 * 0x03 = channel 3
 */
#define PCF8591_CONTROL_AICH_MASK       0x03

/* Initial values */
#define PCF8591_INIT_CONTROL    ((input_mode << 4) | PCF8591_CONTROL_AOEF)
#define PCF8591_INIT_AOUT       0       /* DAC out = 0 */

/* Conversions */
#define REG_TO_SIGNED(reg)      (((reg) & 0x80) ? ((reg) - 256) : (reg))

static int period = 125; // us

struct my_work_t
{
	struct work_struct my_work;
	struct pcf8591_data *    data;
};

struct pcf8591_data 
{
	struct i2c_client *client;
	struct snd_card * card;
	struct snd_pcm * pcm;
	struct snd_pcm_substream *substream;
	
        struct mutex update_lock;
	
        u8 control;
        u8 aout;
	
	struct timer_list htimer;
	struct workqueue_struct *wq;
	struct my_work_t *work;
	int offset;
};
  
static void pcf8591_init_client(struct i2c_client *client)
{
	struct pcf8591_data *data = i2c_get_clientdata(client);
	
        data->control = PCF8591_INIT_CONTROL;
        data->aout = PCF8591_INIT_AOUT;
	data->client = client;
	
	i2c_smbus_write_byte_data(client, data->control, data->aout);
	i2c_smbus_read_byte(client); 
}
 
static int pcf8591_read_channel(struct pcf8591_data *data, int channel)
{	
        u8 value = 0;
	
        mutex_lock(&data->update_lock);
        if ((data->control & PCF8591_CONTROL_AICH_MASK) != channel) 
	{
		data->control = (data->control & ~PCF8591_CONTROL_AICH_MASK) | channel;
		i2c_smbus_write_byte(data->client, data->control);
		i2c_smbus_read_byte(data->client); 
        }
 	value = i2c_smbus_read_byte(data->client);
        mutex_unlock(&data->update_lock);
	
        if ((channel == 2 && input_mode == 2) || (channel != 3 && (input_mode == 1 || input_mode == 3)))
                 return 10 * REG_TO_SIGNED(value);
        else
                 return 10 * value;
}

static void timer_function(unsigned long ptr)
{
	struct pcf8591_data *data = (struct pcf8591_data *)(ptr);	
	queue_work(data->wq, (struct work_struct *)data->work);
	
	mod_timer(&data->htimer, jiffies + usecs_to_jiffies(period));
}

static void pcf8591_work(struct work_struct *work)
{
	struct my_work_t *my_work = (struct my_work_t *)work;
	struct pcf8591_data *data =  my_work->data;
	struct snd_pcm_runtime *runtime = NULL;
	
	if (data->substream)
	{
		runtime = data->substream->runtime;
		if (data->offset >= 128) data->offset = 0;
		runtime->dma_area[data->offset++] = pcf8591_read_channel(data,0);
		runtime->dma_area[data->offset++] = pcf8591_read_channel(data,1);
		snd_pcm_period_elapsed(data->substream);
	}
}

static struct snd_pcm_hardware snd_snd_pcf8591_capture_hw = {
          .info = (SNDRV_PCM_INFO_INTERLEAVED  |  SNDRV_PCM_INFO_BLOCK_TRANSFER ),
          .formats =          SNDRV_PCM_FMTBIT_U8,
          .rates =            SNDRV_PCM_RATE_8000,
          .rate_min =         8000,
          .rate_max =         8000,
          .channels_min =     1,
          .channels_max =     4,
          .buffer_bytes_max = 32768,
          .period_bytes_min = 1024,
          .period_bytes_max = 32768,
          .periods_min =      1,
          .periods_max =      1024,
  };
  
static int snd_pcf8591_capture_open(struct snd_pcm_substream *substream)
{
        struct pcf8591_data *data = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;	
	
	printk("snd_pcf8591_capture_open data:%X substream:%X\n", (unsigned int)data, (unsigned int)substream);
	data->substream = substream;
	data->offset = 0;
	
	/* fill hardware */
	runtime->hw = snd_snd_pcf8591_capture_hw;
	
	/* start timer */
	printk("snd_pcf8591_capture_open setup_timer\n");			 
	setup_timer( &data->htimer, timer_function, (unsigned long)data);
	if (mod_timer( &data->htimer, jiffies + usecs_to_jiffies(period))) 
	{
		printk("Error in mod_timer\n");	
	}
	
	return 0;
}

static int snd_pcf8591_capture_close(struct snd_pcm_substream *substream)
{
        struct pcf8591_data *data = snd_pcm_substream_chip(substream);
	printk("snd_pcf8591_capture_close data:%X substream:%X\n", (unsigned int)data, (unsigned int)substream);
	data->substream = NULL;	
	
	del_timer(&data->htimer);	
	return 0;
}

static int snd_pcf8591_hw_params(struct snd_pcm_substream *substream,
                               struct snd_pcm_hw_params *hw_params)
{
        struct pcf8591_data *data = snd_pcm_substream_chip(substream);
	printk("snd_pcf8591_hw_params data:%X\n", (unsigned int)data);		
	printk("snd_pcf8591_hw_params flags:%X\n", hw_params->flags);		
	printk("snd_pcf8591_hw_params rmask:%X\n", hw_params->rmask);		
	printk("snd_pcf8591_hw_params cmask:%X\n", hw_params->cmask);		
	printk("snd_pcf8591_hw_params rate:%d/%d\n", hw_params->rate_num,hw_params->rate_den);		
	printk("snd_pcf8591_hw_params params_buffer_bytes(hw_params):%d\n", params_buffer_bytes(hw_params));		
        return snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(hw_params));
}

static int snd_pcf8591_prepare(struct snd_pcm_substream *substream)
{
        struct pcf8591_data *data = snd_pcm_substream_chip(substream);
	printk("snd_pcf8591_prepare data:%X\n", (unsigned int)data);	
	return 0;
}

static int snd_pcf8591_trigger(struct snd_pcm_substream *substream, int cmd)
{
        struct pcf8591_data *data = snd_pcm_substream_chip(substream);
	printk("snd_pcf8591_trigger data:%X\n", (unsigned int)data);		
	return 0;
}

static snd_pcm_uframes_t snd_pcf8591_capture_pointer(struct snd_pcm_substream *substream)
{
        struct pcf8591_data *data = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;	
	unsigned int ptr = data->offset;
	
//	printk("snd_pcf8591_capture_pointer data:%X offset:%d\n", (unsigned int)data, ptr);		

        return bytes_to_frames(runtime, ptr);
}
	
static struct snd_pcm_ops pcm_capture_ops = {
        .open =         snd_pcf8591_capture_open,
        .close =        snd_pcf8591_capture_close,
        .ioctl =        snd_pcm_lib_ioctl,
        .hw_params =    snd_pcf8591_hw_params,
        .hw_free =      snd_pcm_lib_free_pages,
        .prepare =      snd_pcf8591_prepare,
        .trigger =      snd_pcf8591_trigger,
        .pointer =      snd_pcf8591_capture_pointer,
};

static int pcf8591_probe(struct i2c_client *client, const struct i2c_device_id *i2cid)
{
	struct pcf8591_data *data = NULL;
        int err = 0;
	 
	printk("pcf8591_probe %s %X %s\n", i2cid->name, client->addr << 1, client->adapter->name);			 
 
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -EIO;

        /* Initialize the PCF8591 data structure */
        data = devm_kzalloc(&client->dev, sizeof(struct pcf8591_data), GFP_KERNEL);
        if (!data)
	{
		printk("devm_kzalloc fails\n");		
                return -ENOMEM;
	} 
        i2c_set_clientdata(client, data);
        mutex_init(&data->update_lock);

        /* Initialize the PCF8591 chip */
	printk("pcf8591_init_client %s %X\n", i2cid->name, (unsigned int)client);			 
        pcf8591_init_client(client);	
	
	/* create the SND card */
	printk("snd_card_create %s\n", i2cid->name);			 
	err = snd_card_new(&client->dev, index, id, THIS_MODULE, 0, &data->card);
        if (err < 0)
	{
		printk("snd_card_create fails :%d\n",err);			 
		return err;	 
	}
	snd_card_set_id(data->card,KBUILD_MODNAME);
	strcpy(data->card->driver, KBUILD_MODNAME);
	strcpy(data->card->shortname, KBUILD_MODNAME);
	
	/* create PCM capture device */
	printk("snd_pcm_new %s\n", i2cid->name);			 
	err = snd_pcm_new(data->card, KBUILD_MODNAME "_PCM", 0, 0, 1, &data->pcm);
        if (err < 0) 
	{
		printk("snd_pcm_new fails :%d\n",err);			 
                return err;
        }
	sprintf(data->pcm->name, "DSP");
        data->pcm->private_data = data;	
	snd_pcm_set_ops(data->pcm, SNDRV_PCM_STREAM_CAPTURE, &pcm_capture_ops);
	
	err = snd_pcm_lib_preallocate_pages_for_all(data->pcm, SNDRV_DMA_TYPE_CONTINUOUS, snd_dma_continuous_data(GFP_KERNEL), 0, 64*1024);
        if (err < 0) 
	{
		printk("snd_pcm_lib_preallocate_pages_for_all fails :%d\n",err);			 
                return err;
	}

	/* create workqueue */
	data->wq = create_singlethread_workqueue(KBUILD_MODNAME);
        if (data->wq == NULL) 
	{
		err = -ENOMEM;
		printk("create_singlethread_workqueue fails :%d\n",err);	
                return err;
        }
	data->work = (struct my_work_t *)kmalloc(sizeof(struct my_work_t), GFP_KERNEL);
	if (!data->work)
	{
		err = -ENOMEM;
		printk("kmalloc fails :%d\n",err);	
                return err;
	}
	INIT_WORK( (struct work_struct *)data->work, pcf8591_work );
	data->work->data = data;

	/* register the card */
	printk("snd_card_register %s\n", i2cid->name);			 
	err = snd_card_register(data->card);
        if (err < 0) 
	{
		printk("snd_card_register fails :%d\n",err);	
		snd_card_free(data->card);		
                return err;
        }
	 				
        return 0; 
 }
 
 static int pcf8591_remove(struct i2c_client *client)
 {
        struct pcf8591_data *data = i2c_get_clientdata(client);
 	 
	destroy_workqueue(data->wq);
	kfree( (void *)data->work );
	snd_card_disconnect(data->card);
	mutex_destroy (&data->update_lock);
        return 0;
 }
  
 static const struct i2c_device_id pcf8591_id[] = {
         { "pcf8591", 0 },
         { }
 };
 MODULE_DEVICE_TABLE(i2c, pcf8591_id);
 
 static struct i2c_driver pcf8591_driver = {
         .driver = {
		.name	= KBUILD_MODNAME, 		 
         },
         .probe          = pcf8591_probe,
         .remove         = pcf8591_remove,
         .id_table       = pcf8591_id,
 };
 
 static int __init pcf8591_init(void)
 {
        if (input_mode < 0 || input_mode > 3) 
	{
                pr_warn("invalid input_mode (%d)\n", input_mode);
                input_mode = 0;
        }
        return i2c_add_driver(&pcf8591_driver);
 }
 
 static void __exit pcf8591_exit(void)
 {
         i2c_del_driver(&pcf8591_driver);
 }
 
 MODULE_DESCRIPTION("ALSA PCF8591 driver");
 MODULE_LICENSE("GPL");
 
 module_init(pcf8591_init);
 module_exit(pcf8591_exit);
 
