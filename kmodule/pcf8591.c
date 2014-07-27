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

#include <asm/io.h>

#include <sound/core.h>
#include <sound/initval.h>
#include <sound/i2c.h>
#include <sound/pcm.h>

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX; 
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR; 

/* Insmod parameters */

static int input_mode;
module_param(input_mode, int, 0);
MODULE_PARM_DESC(input_mode,
        "Analog input mode:\n"
        " 0 = four single ended inputs\n"
        " 1 = three differential inputs\n"
        " 2 = single ended and differential mixed\n"
        " 3 = two differential inputs\n");

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

static int period = 1250; // ms
struct pcf8591_data 
{
	struct i2c_client *client;
	struct snd_card * card;
	struct snd_pcm * pcm;
	int dsp_minor;
		
        struct mutex update_lock;
	
        u8 control;
        u8 aout;
	
	struct timer_list htimer;
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
	printk("pcf8591_read_channel date:%X client:%X channel:%d\n",  data, data->client, channel);
        mutex_lock(&data->update_lock);

        if ((data->control & PCF8591_CONTROL_AICH_MASK) != channel) 
	{
		printk("select channel:%d\n",channel);	
		data->control = (data->control & ~PCF8591_CONTROL_AICH_MASK) | channel;
		i2c_smbus_write_byte(data->client, data->control);
		i2c_smbus_read_byte(data->client); 
        }
 	value = i2c_smbus_read_byte(data->client);
 
        mutex_unlock(&data->update_lock);
	printk("pcf8591_read_channel value:%d\n", value);		
        if ((channel == 2 && input_mode == 2) || (channel != 3 && (input_mode == 1 || input_mode == 3)))
                 return 10 * REG_TO_SIGNED(value);
        else
                 return 10 * value;
}

static void timer_function(unsigned long ptr)
{
	struct pcf8591_data *data = (struct pcf8591_data *)(ptr);
	printk("timer_function dev:%d date:%X client:%X\n",data->dsp_minor, data, data->client);		
//	pcf8591_read_channel(data,0);		
		
	
	mod_timer(&data->htimer, jiffies + msecs_to_jiffies(period));
}

static int dev_open(struct inode *inode, struct file *file)
{
	printk("dev_open :%d,%d\n",imajor(inode),iminor(inode));			 
	return 0;
}
static int dev_release(struct inode *inode, struct file *file)
{
	printk("dev_release :%d,%d\n",imajor(inode),iminor(inode));			 
	return 0;
}

static ssize_t dev_read(struct file *file, char __user *buf, size_t count, loff_t *off)
{
	struct inode *inode = file_inode(file);
	
	printk("dev_read :%d,%d\n",imajor(inode),iminor(inode));			 	
        return 0; 
}

static long dev_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct inode *inode = file_inode(file);
        int val = 0;
        int __user *p = (int __user *)arg;

	printk("dev_ioctl :%d,%d cmd:%X arg:%X\n",imajor(inode),iminor(inode),cmd, arg);			 
	
        switch (cmd) 
	{
		case SNDCTL_DSP_SUBDIVIDE:
		case SNDCTL_DSP_SETFRAGMENT:
		case SNDCTL_DSP_SETDUPLEX:
		case SNDCTL_DSP_POST:
			printk("dev_ioctl : ignore command\n");
			return 0;

		case SNDCTL_DSP_SETFMT:
			if (get_user(val, p))
				return -EFAULT;						
			printk("dev_ioctl : SNDCTL_DSP_SETFMT:%d\n",val);			
			return 0;			
		
		case SNDCTL_DSP_GETBLKSIZE:
			val = 4096;
			if (put_user(val, p))
				return -EFAULT;
			printk("dev_ioctl : SNDCTL_DSP_GETBLKSIZE:%d\n", val);
			return 0;

		case SNDCTL_DSP_GETFMTS:
			val = AFMT_S16_LE | AFMT_U8;
			if (put_user(val, p))
				return -EFAULT;
			printk("dev_ioctl : SNDCTL_DSP_GETFMTS:%d\n", val);
			return 0;

		case SNDCTL_DSP_GETCAPS:
			val = DSP_CAP_DUPLEX | DSP_CAP_BATCH;
			if (put_user(val, p))
				return -EFAULT;
			printk("dev_ioctl : SNDCTL_DSP_GETCAPS:%d\n", val);
			return 0;

		case SNDCTL_DSP_SPEED:
			val = 1000/period;
			if (put_user(val, p))
				return -EFAULT;
			printk("dev_ioctl : SNDCTL_DSP_SPEED:%d\n", val);
			return 0;

		case SNDCTL_DSP_CHANNELS:
		case SNDCTL_DSP_STEREO:
			val = 1;
			if (put_user(val, p))
				return -EFAULT;
			printk("dev_ioctl : SNDCTL_DSP_CHANNELS/SNDCTL_DSP_STEREO:%d\n", val);
			return 0;			
        }

	printk("dev_ioctl : not supported command\n");
        return -EINVAL;
}

static const struct file_operations dev_fileops = {
         .owner          = THIS_MODULE,
         .read           = dev_read,
         .unlocked_ioctl  = dev_ioctl,
         .open           = dev_open,
         .release        = dev_release,
};

static int card_open(struct inode *inode, struct file *file)
{
	printk("card_open :%d,%d\n",imajor(inode),iminor(inode));		
        return 0; 
}

static int card_release(struct inode *inode, struct file *file)
{
	printk("card_release :%d,%d\n",imajor(inode),iminor(inode));			 
	return 0;
}

static ssize_t card_read(struct file *file, char __user *buf, size_t len, loff_t *off)
{
	struct inode *inode = file_inode(file);
	char outputPtr[128];
	
	struct pcf8591_data *data = snd_lookup_minor_data(iminor(inode),SNDRV_DEVICE_TYPE_PCM_CAPTURE);		
	printk("card_open :%d,%d data:%X minor:%d\n",imajor(inode),iminor(inode),data, data->dsp_minor);			 

	snprintf(outputPtr, sizeof(outputPtr)-1,"%d\n",pcf8591_read_channel(data,0));
        if (copy_to_user(buf, outputPtr, strlen(outputPtr)))
	{
		printk("copy_to_user fails\n");		
		return -EFAULT;
	}
	
	return strlen(outputPtr);
}

static const struct file_operations card_fileops = {
         .owner          = THIS_MODULE,
         .open           = card_open,
         .read           = card_read,
         .release        = card_release,
};

static int pcf8591_probe(struct i2c_client *client, const struct i2c_device_id *i2cid)
{
	struct pcf8591_data *data = NULL;
        int err = 0;
	int dev = 1;
	 
	printk("pcf8591_probe %s %X %s\n", i2cid->name, client->addr << 1, client->adapter->name);			 
 
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -EIO;

        data = devm_kzalloc(&client->dev, sizeof(struct pcf8591_data), GFP_KERNEL);
        if (!data)
	{
		printk("devm_kzalloc fails\n");		
                return -ENOMEM;
	}
 
        i2c_set_clientdata(client, data);
        mutex_init(&data->update_lock);
   
	/* create the SND card */
	printk("snd_card_create %s\n", i2cid->name);			 
	err = snd_card_create(index[dev], id[dev], THIS_MODULE, 0, &data->card);
        if (err < 0)
	{
		printk("snd_card_create fails :%d\n",err);			 
		return err;	 
	}
	snd_card_set_id(data->card,KBUILD_MODNAME);
	strcpy(data->card->driver, KBUILD_MODNAME);
	strcpy(data->card->shortname, KBUILD_MODNAME);
	
	printk("snd_pcm_new %s\n", i2cid->name);			 
	err = snd_pcm_new(data->card, "Capture", 0, 0, 1, &data->pcm);
        if (err < 0) 
	{
		printk("snd_pcm_new fails :%d\n",err);			 
                return err;
        }
	
	printk("snd_card_register %s\n", i2cid->name);			 
	err = snd_card_register(data->card);
        if (err < 0) 
	{
		printk("snd_card_register fails :%d\n",err);	
		snd_card_free(data->card);		
                return err;
        }	 
	 
        /* Initialize the PCF8591 chip */
	printk("pcf8591_init_client %s %X\n", i2cid->name, client);			 
        pcf8591_init_client(client);
	
	/* create DSP device*/	 
	printk("register_sound_dsp %s\n", i2cid->name);			 
	if ((data->dsp_minor = register_sound_dsp(&dev_fileops, -1)) < 0) 
	{	
		printk("register_sound_dsp\n");		
	}
	
	/* register device */
	printk("snd_register_device %s %d\n", i2cid->name, data->card->number);			 
	err = snd_register_device(SNDRV_DEVICE_TYPE_PCM_CAPTURE, data->card, data->card->number, &card_fileops, data, KBUILD_MODNAME);
        if (err < 0) 
	{
		printk("snd_register_device fails :%d\n",err);	
		snd_card_free(data->card);		
                return err;
        }	 
	
	pcf8591_read_channel(data,0);
	
	/* start timer */
	printk("setup_timer %s\n", i2cid->name);			 
	setup_timer( &data->htimer, timer_function, (unsigned long)data);
	if (mod_timer( &data->htimer, jiffies + msecs_to_jiffies(period))) 
	{
		printk("Error in mod_timer\n");	
	}
	
        return 0; 
 }
 
 static int pcf8591_remove(struct i2c_client *client)
 {
        struct pcf8591_data *data = i2c_get_clientdata(client);
 	 
	del_timer(&data->htimer);
	unregister_sound_dsp(data->dsp_minor);
	snd_unregister_device(SNDRV_DEVICE_TYPE_PCM_CAPTURE,data->card,data->card->number);
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
         if (input_mode < 0 || input_mode > 3) {
                 pr_warn("invalid input_mode (%d)\n", input_mode);
                 input_mode = 0;
         }
         return i2c_add_driver(&pcf8591_driver);
 }
 
 static void __exit pcf8591_exit(void)
 {
         i2c_del_driver(&pcf8591_driver);
 }
 
 MODULE_AUTHOR("MPR");
 MODULE_DESCRIPTION("ALSA PCF8591 driver");
 MODULE_LICENSE("GPL");
 
 module_init(pcf8591_init);
 module_exit(pcf8591_exit);
 
