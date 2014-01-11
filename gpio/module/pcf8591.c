/*
  ALSA PCF8591 
*/

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/mutex.h>
#include <linux/err.h>
#include <linux/sound.h>
#include <linux/soundcard.h>

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

struct pcf8591_data {
	struct snd_card * card;
	struct snd_pcm * pcm;
	int dsp_minor;
	
	struct snd_i2c_bus * bus;
	struct snd_i2c_device * device;
	
        struct mutex update_lock;
	
        u8 control;
        u8 aout;
};

static void pcf8591_init_client(struct i2c_client *client);
static int pcf8591_read_channel(struct device *dev, int channel);

/* following are the sysfs callback functions */
#define show_in_channel(channel)                                        \
static ssize_t show_in##channel##_input(struct device *dev,             \
                                        struct device_attribute *attr,  \
                                        char *buf)                      \
{                                                                       \
        return sprintf(buf, "%d\n", pcf8591_read_channel(dev, channel));\
}                                                                       \
static DEVICE_ATTR(in##channel##_input, S_IRUGO,                        \
                   show_in##channel##_input, NULL);
 
show_in_channel(0);
show_in_channel(1);
show_in_channel(2);
show_in_channel(3);

/* ------------------------------------ */
static ssize_t show_out0_ouput(struct device *dev,
                                struct device_attribute *attr, char *buf)
{
         struct pcf8591_data *data = i2c_get_clientdata(to_i2c_client(dev));
         return sprintf(buf, "%d\n", data->aout * 10);
}
 
static ssize_t set_out0_output(struct device *dev,
                                struct device_attribute *attr,
                                const char *buf, size_t count)
{
         unsigned long val;
         struct i2c_client *client = to_i2c_client(dev);
         struct pcf8591_data *data = i2c_get_clientdata(client);
         int err;
 
         err = kstrtoul(buf, 10, &val);
         if (err)
                 return err;
 
         val /= 10;
         if (val > 255)
                 return -EINVAL;
 
         data->aout = val;
         i2c_smbus_write_byte_data(client, data->control, data->aout);
         return count;
}
 
static DEVICE_ATTR(out0_output, S_IWUSR | S_IRUGO, show_out0_ouput, set_out0_output);
/* ------------------------------------ */ 

static ssize_t show_out0_enable(struct device *dev,
                                 struct device_attribute *attr, char *buf)
{
         struct pcf8591_data *data = i2c_get_clientdata(to_i2c_client(dev));
         return sprintf(buf, "%u\n", !(!(data->control & PCF8591_CONTROL_AOEF)));
}
 
static ssize_t set_out0_enable(struct device *dev,
                                struct device_attribute *attr,
                                const char *buf, size_t count)
{
         struct i2c_client *client = to_i2c_client(dev);
         struct pcf8591_data *data = i2c_get_clientdata(client);
         unsigned long val;
         int err;
 
         err = kstrtoul(buf, 10, &val);
         if (err)
                 return err;
 
         mutex_lock(&data->update_lock);
         if (val)
                 data->control |= PCF8591_CONTROL_AOEF;
         else
                 data->control &= ~PCF8591_CONTROL_AOEF;
         i2c_smbus_write_byte(client, data->control);
         mutex_unlock(&data->update_lock);
         return count;
}
 
static DEVICE_ATTR(out0_enable, S_IWUSR | S_IRUGO, show_out0_enable, set_out0_enable);
/* ------------------------------------ */

static struct attribute *pcf8591_attributes[] = {
         &dev_attr_out0_enable.attr,
         &dev_attr_out0_output.attr,
         &dev_attr_in0_input.attr,
         &dev_attr_in1_input.attr,
         NULL
};
 
static const struct attribute_group pcf8591_attr_group = {
         .attrs = pcf8591_attributes,
};
 
static struct attribute *pcf8591_attributes_opt[] = {
         &dev_attr_in2_input.attr,
         &dev_attr_in3_input.attr,
         NULL
};
 
static const struct attribute_group pcf8591_attr_group_opt = {
         .attrs = pcf8591_attributes_opt,
};
  
static int dev_open(struct inode *inode, struct file *file)
{
        int minor = iminor(inode);
        int err = 0;
	return err;
}
static int dev_release(struct inode *inode, struct file *file)
{
        int minor = iminor(inode);
        int err = 0;
	return err;
}

static ssize_t dev_read(struct file *file, char __user *buf, size_t count, loff_t *off)
{
        int minor = iminor(file_inode(file));
	struct snd_pcm *pcm;
	
	pcm = snd_lookup_oss_minor_data(minor,SNDRV_OSS_DEVICE_TYPE_PCM);	
	
        return 0; // 	snd_i2c_readbytes(device,buf,count);
}

static ssize_t dev_write(struct file *file, const char __user *buf, size_t count, loff_t *off)
{
        int minor = iminor(file_inode(file));
        return -EINVAL;
}

static int dev_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
        int val;
        int __user *p = (int __user *)arg;

        switch (cmd) {
        case SNDCTL_DSP_SUBDIVIDE:
        case SNDCTL_DSP_SETFRAGMENT:
        case SNDCTL_DSP_SETDUPLEX:
        case SNDCTL_DSP_POST:
                return 0;

        case SNDCTL_DSP_GETIPTR:
        case SNDCTL_DSP_GETOPTR:
        case SNDCTL_DSP_MAPINBUF:
        case SNDCTL_DSP_MAPOUTBUF:
        case SNDCTL_DSP_GETOSPACE:
        case SNDCTL_DSP_GETISPACE:
        case SNDCTL_DSP_RESET:
        case SNDCTL_DSP_SYNC:
        case SNDCTL_DSP_SETFMT:
        case SNDCTL_DSP_NONBLOCK:
                return -EINVAL;

        case SNDCTL_DSP_GETBLKSIZE:
                val = 4096;
                if (put_user(val, p))
                        return -EFAULT;
                return 0;

        case SNDCTL_DSP_GETFMTS:
                val = AFMT_S16_LE | AFMT_U8;
                if (put_user(val, p))
                        return -EFAULT;
                return 0;

        case SNDCTL_DSP_GETCAPS:
                val = DSP_CAP_DUPLEX | DSP_CAP_BATCH;
                if (put_user(val, p))
                        return -EFAULT;
                return 0;

        case SNDCTL_DSP_SPEED:
                val = 8000;
                if (put_user(val, p))
                        return -EFAULT;
                return 0;

        case SNDCTL_DSP_CHANNELS:
        case SNDCTL_DSP_STEREO:
		val = 1;
                if (put_user(val, p))
                        return -EFAULT;
                return 0;
        }

        return -EINVAL;
}

static const struct file_operations dev_fileops = {
         .owner          = THIS_MODULE,
         .read           = dev_read,
         .write          = dev_write,
         .unlocked_ioctl  = dev_ioctl,
         .open           = dev_open,
         .release        = dev_release,
};

static int pcf8591_probe(struct i2c_client *client,
                          const struct i2c_device_id *i2cid)
{
         struct pcf8591_data *data;
         int err = 0;
	 int dev = 1;
	 
	printk("pcf8591_probe\n");			 
 
         data = devm_kzalloc(&client->dev, sizeof(struct pcf8591_data), GFP_KERNEL);
         if (!data)
	 {
		printk("devm_kzalloc fails\n");		
                return -ENOMEM;
	 }
 
         i2c_set_clientdata(client, data);
         mutex_init(&data->update_lock);
 
         /* Initialize the PCF8591 chip */
         pcf8591_init_client(client);
 
         /* Register sysfs hooks */
         err = sysfs_create_group(&client->dev.kobj, &pcf8591_attr_group);
         if (err)
	 {
		 printk("sysfs_create_group fails :%d\n",err);
                 return err;
	 }
 
         /* Register input2 if not in "two differential inputs" mode */
         if (input_mode != 3) {
                 err = device_create_file(&client->dev, &dev_attr_in2_input);
                 if (err)
		 {
			printk("device_create_file fails :%d\n",err);
			goto exit_sysfs_remove;
		 }
         }
 
         /* Register input3 only in "four single ended inputs" mode */
         if (input_mode == 0) {
                 err = device_create_file(&client->dev, &dev_attr_in3_input);
                 if (err)
		 {
			printk("device_create_file fails :%d\n",err);			 
			goto exit_sysfs_remove;
		 }
         }
 
	/* create the SND card */
	err = snd_card_create(index[dev], id[dev], THIS_MODULE, 0, &data->card);
         if (err < 0)
	 {
		printk("snd_card_create fails :%d\n",err);			 
		return err;	 
	 }
	snd_card_set_id(data->card,KBUILD_MODNAME);
	strcpy(data->card->driver, KBUILD_MODNAME);
	strcpy(data->card->shortname, KBUILD_MODNAME);
	err = snd_pcm_new(data->card, "Capture", 0, 0, 1, &data->pcm);
        if (err < 0) 
	{
		printk("snd_pcm_new fails :%d\n",err);			 
                return err;
        }	 
	err = snd_card_register(data->card);
        if (err < 0) 
	{
		printk("snd_card_register fails :%d\n",err);	
		snd_card_free(data->card);		
                return err;
         }	 
	 
	/* create the I2C bus */
         err = snd_i2c_bus_create(data->card, "BUS", NULL, &data->bus);
         if (err < 0)
	 {
		printk("snd_i2c_bus_create fails :%d\n",err);			 
		return err;
	 }
  
         /* create the I2C device */
         err = snd_i2c_device_create(data->bus, KBUILD_MODNAME, client->addr, &data->device);
         if (err < 0)
	 {
		printk("snd_i2c_device_create fails :%d\n",err);			 
		return err;
	 }

	/* create DSP device*/	 
	if ((data->dsp_minor = register_sound_dsp(&dev_fileops, -1)) < 0) 
	{	
		printk("register_sound_dsp\n");		
	}
		 	 
         return 0;
 
 exit_sysfs_remove:
         sysfs_remove_group(&client->dev.kobj, &pcf8591_attr_group_opt);
         sysfs_remove_group(&client->dev.kobj, &pcf8591_attr_group);
         return err;
 }
 
 static int pcf8591_remove(struct i2c_client *client)
 {
        struct pcf8591_data *data = i2c_get_clientdata(client);
 
        sysfs_remove_group(&client->dev.kobj, &pcf8591_attr_group_opt);
        sysfs_remove_group(&client->dev.kobj, &pcf8591_attr_group);
	 
	unregister_sound_dsp(data->dsp_minor);
	snd_i2c_device_free(data->device);
	snd_card_disconnect(data->card);	 
        return 0;
 }
 
 /* Called when we have found a new PCF8591. */
 static void pcf8591_init_client(struct i2c_client *client)
 {
         struct pcf8591_data *data = i2c_get_clientdata(client);
         data->control = PCF8591_INIT_CONTROL;
         data->aout = PCF8591_INIT_AOUT;
 
         i2c_smbus_write_byte_data(client, data->control, data->aout);
 
         /*
          * The first byte transmitted contains the conversion code of the
          * previous read cycle. FLUSH IT!
          */
         i2c_smbus_read_byte(client);
 }
 
 static int pcf8591_read_channel(struct device *dev, int channel)
 {
         u8 value;
         struct i2c_client *client = to_i2c_client(dev);
         struct pcf8591_data *data = i2c_get_clientdata(client);
 
         mutex_lock(&data->update_lock);
 
         if ((data->control & PCF8591_CONTROL_AICH_MASK) != channel) {
                 data->control = (data->control & ~PCF8591_CONTROL_AICH_MASK)
                               | channel;
                 i2c_smbus_write_byte(client, data->control);
 
                 /*
                  * The first byte transmitted contains the conversion code of
                  * the previous read cycle. FLUSH IT!
                  */
                 i2c_smbus_read_byte(client);
         }
         value = i2c_smbus_read_byte(client);
 
         mutex_unlock(&data->update_lock);
 
         if ((channel == 2 && input_mode == 2) ||
             (channel != 3 && (input_mode == 1 || input_mode == 3)))
                 return 10 * REG_TO_SIGNED(value);
         else
                 return 10 * value;
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
 
