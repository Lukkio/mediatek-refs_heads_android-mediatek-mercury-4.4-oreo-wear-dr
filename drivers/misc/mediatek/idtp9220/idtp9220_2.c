/*
	
 * @file   idtp9220.c
	
 * @author  <roy@ROY-PC>
	
 * @date   Sun Nov 22 11:50:06 2015
	
 *
	
 * @brief
	
 *
	
 *
	
 */
	
#include <linux/module.h>	
#include <linux/kernel.h>	
#include <linux/slab.h>	
#include <linux/delay.h>	
#include <linux/sched.h>	
#include <linux/i2c.h>	
#include <linux/workqueue.h>	
#include <linux/sysfs.h>	
#include <asm/unaligned.h>	
/*add for sdm845 request*/
#include "idtp9220_2.h"	
#include <linux/regmap.h>	
#include <linux/spinlock.h>	
#include <linux/of_gpio.h>	
#include <linux/of_device.h>	
#include <linux/irq.h>	
#include <linux/interrupt.h>	
#include <linux/power_supply.h>	
#include <linux/switch.h>

#include <mach/mt_gpio.h>
#include <mach/mt_reg_base.h>
#include <mach/mt_typedefs.h>
#include "cust_gpio_usage.h"


#define DEVICE_BUS  1
#define DEVICE_ADDR 0x61
	
static struct idtp9220_device_info *g_di;
	
	
struct idtp9220_access_func {
	
	int (*read)(struct idtp9220_device_info *di, u16 reg, u8 *val);
	
	int (*write)(struct idtp9220_device_info *di, u16 reg, u8 val);
	
	int (*read_buf)(struct idtp9220_device_info *di,
	
				u16 reg, u8 *buf, u32 size);
	
	int (*write_buf)(struct idtp9220_device_info *di,
	
				u16 reg, u8 *buf, u32 size);
	
};
	
	
struct idtp9220_dt_props {
	
	unsigned int irq_gpio;
	
	unsigned int enable_gpio;
	
};
	
	
struct idtp9220_device_info {
	
	int chip_enable;
	
	char *name;
	
	struct device *dev;
	
	struct idtp9220_access_func bus;
	
	struct regmap    *regmap;
	
	struct idtp9220_dt_props dt_props;
	
	int irq;
	
	struct delayed_work	irq_work;
	
	struct pinctrl *idt_pinctrl;
	
	struct pinctrl_state *idt_gpio_active;
	
	struct pinctrl_state *idt_gpio_suspend;
	
	struct power_supply	*usb_psy;
	
	struct power_supply	*dc_psy;
	
	struct power_supply	*batt_psy;
	
	struct power_supply	*idtp_psy;
	
	struct mutex	read_lock;
	
	struct mutex	write_lock;
	
	struct delayed_work	chg_monitor_work;
	
	struct delayed_work	chg_detect_work;
	
#ifdef IDTP9220_SRAM_UPDATE
	
	struct delayed_work	sram_update_work;
	
#endif
	
	int tx_charger_type;
	
	int status;
	
	int count_5v;
	
	int count_9v;
	
	int exchange;
	
	int dcin_present;
	
	
	/*idt9220 charging info*/
	
	int vout;
	
	int iout;
	
	int f;
	
	int vrect;
	
	int ss;
	
};
	
	
void idtp922x_request_adapter(struct idtp9220_device_info *di);
	
static void idtp9220_set_charging_param(struct idtp9220_device_info *di);
	
	
/*static int idt_signal_strength = 0;
	
module_param_named(ss, idt_signal_strength, int, 0600);
	
*/
	
	
static int idt_signal_range = 2;
	
module_param_named(signal_range, idt_signal_range, int, 0644);
	
	
	
int idtp9220_read(struct idtp9220_device_info *di, u16 reg, u8 *val) {
	
	unsigned int temp;
	
	int rc;
	
	
	mutex_lock(&di->read_lock);
	
	rc = regmap_read(di->regmap, reg, &temp);
	
	if (rc >= 0)
	
		*val = (u8)temp;
	
	
	mutex_unlock(&di->read_lock);
	
	return rc;
	
}
	
	
int idtp9220_write(struct idtp9220_device_info *di, u16 reg, u8 val) {
	
	int rc = 0;
	
	
	mutex_lock(&di->write_lock);
	
	rc = regmap_write(di->regmap, reg, val);
	
	if (rc < 0)
	
		dev_err(di->dev, "[idt] idtp9220 write error: %d\n", rc);
	
	
	mutex_unlock(&di->write_lock);
	
	return rc;
	
}
	
	
int idtp9220_read_buffer(struct idtp9220_device_info *di, u16 reg, u8 *buf, u32 size) {
	
	int rc = 0;
	
	
	while (size--) {
	
		rc = di->bus.read(di, reg++, buf++);
	
		if (rc < 0) {
	
			dev_err(di->dev, "[idt] write error: %d\n", rc);
	
			return rc;
	
		}
	
	}
	
	
	return rc;
	
}
	
	
int idtp9220_write_buffer(struct idtp9220_device_info *di, u16 reg, u8 *buf, u32 size) {
	
	int rc = 0;
	
	
	while (size--) {
	
		rc = di->bus.write(di, reg++, *buf++);
	
		if (rc < 0) {
	
			dev_err(di->dev, "[idt] write error: %d\n", rc);
	
			return rc;
	
		}
	
	}
	
	
	return rc;
	
}
	
	
u32 ExtractPacketSize(u8 hdr) {
	
	if (hdr < 0x20)
	
		return 1;
	
	if (hdr < 0x80)
	
		return (2 + ((hdr - 0x20) >> 4));
	
	if (hdr < 0xe0)
	
		return (8 + ((hdr - 0x80) >> 3));
	
	return (20 + ((hdr - 0xe0) >> 2));
	
}
	
	
void idtp922x_clrInt(struct idtp9220_device_info *di, u8 *buf, u32 size) {
	
	di->bus.write_buf(di, REG_SSINTCLR, buf, size);
	
	di->bus.write(di, REG_SSCMND, CLRINT);
	
}
	
	
void idtp922x_sendPkt(struct idtp9220_device_info *di, ProPkt_Type *pkt) {
	
	u32 size = ExtractPacketSize(pkt->header)+1;
	
	di->bus.write_buf(di, REG_PROPPKT, (u8 *)pkt, size);
	
	di->bus.write(di, REG_SSCMND, SENDPROPP);
	
}
	
	
void idtp922x_receivePkt(struct idtp9220_device_info *di, u8 *buf) {
	
	u8 header;
	
	int rc;
	
	u32 size;
	
	
	di->bus.read(di, REG_BCHEADER, &header);
	
	size = ExtractPacketSize(header)+1;
	
	rc = di->bus.read_buf(di, REG_BCDATA, buf, size);
	
	if (rc < 0)
	
		dev_err(di->dev, "[idt] read Tx data error: %d\n", rc);
	
}
	
	
void idtp922x_set_adap_vol(struct idtp9220_device_info *di, u16 mv)
	
{
	
	di->bus.write(di, REG_FC_VOLTAGE_L, mv&0xff);
	
	di->bus.write(di, REG_FC_VOLTAGE_H, (mv>>8)&0xff);
	
	di->bus.write(di, REG_SSCMND, VSWITCH);
	
}
	
	
void idtp922x_set_pmi_icl(struct idtp9220_device_info *di, int mA)
	
{
	
	union power_supply_propval val = {0, };
	
	
	val.intval = mA;
	
	power_supply_set_property(di->dc_psy, POWER_SUPPLY_PROP_CURRENT_MAX, &val);
	
}
	
	
/* Adapter Type */
	
/* Adapter_list = {0x00:'ADAPTER_UNKNOWN',  */
	
/*            0x01:'SDP 500mA',  */
	
/*            0x02:'CDP 1.1A',  */
	
/*            0x03:'DCP 1.5A',  */
	
/*            0x05:'QC2.0',  */
	
/*            0x06:'QC3.0',  */
	
/*            0x07:'PD',} */
	
void idtp922x_request_adapter(struct idtp9220_device_info *di)
	
{
	
	ProPkt_Type pkt;
	
	pkt.header = PROPRIETARY18;
	
	pkt.cmd = BC_ADAPTER_TYPE;
	
	
	idtp922x_sendPkt(di, &pkt);
	
}
	
	
static int idtp9220_get_vout(struct idtp9220_device_info *di)
	
{
	
	u8 vout_l, vout_h;
	
	
	if (!di)
	
		return 0;
	
	
	di->bus.read(di, REG_ADC_VOUT_L, &vout_l);
	
	di->bus.read(di, REG_ADC_VOUT_H, &vout_h);
	
	di->vout = vout_l | ((vout_h & 0xf)<< 8);
	
	di->vout = di->vout * 6 * 21 * 1000 / 40950 + ADJUST_METE_MV;
	
	
	return di->vout;
	
}
	
	
static void idtp9220_set_vout(struct idtp9220_device_info *di, int index)
	
{
	
	if (!di)
	
		return;
	
	
	di->bus.write(di, REG_VOUT_SET, index);
	
}
	
	
static int idtp9220_get_iout(struct idtp9220_device_info *di)
	
{
	
	u8 cout_l, cout_h;
	
	
	if (!di)
	
		return 0;
	
	
	di->bus.read(di, REG_RX_LOUT_L, &cout_l);
	
	di->bus.read(di, REG_RX_LOUT_H, &cout_h);
	
	di->iout = cout_l | (cout_h << 8);
	
	
	return di->iout;
	
}
	
	
static int idtp9220_get_freq(struct idtp9220_device_info *di)
	
{
	
	u8 data_list[2];
	
	
	di->bus.read_buf(di, REG_FREQ_ADDR, data_list, 2);
	
	di->f = 64*HSCLK/(data_list[0] | (data_list[1] << 8))/10;
	
	
	return di->f;
	
}
	
	
static int idtp9220_get_vrect(struct idtp9220_device_info *di)
	
{
	
	u8 data_list[2];
	
	
	di->bus.read_buf(di, REG_ADC_VRECT, data_list, 2);
	
	di->vrect = data_list[0] | ((data_list[1] & 0xf)<< 8);
	
	di->vrect = di->vrect * 10 * 21 * 1000 / 40950;
	
	
	return di->vrect;
	
}
	
	
static int idtp9220_get_signal_strength(struct idtp9220_device_info *di)
	
{
	
	u8 ss;
	
	
	di->bus.read(di, REG_SIGNAL_STRENGTH, &ss);
	
	if (ss >= 75)
	
		idt_signal_range = 1;
	
	else
	
		idt_signal_range = 0;
	
	
	dev_info(di->dev, "[idt] signal strength: %d\n", ss);
	printk(KERN_ALERT "[idt] signal strength: %d\n", ss);
	
	di->ss = idt_signal_range;
	
	
	return ss;
	
}
	
	
static void idtp9220_send_device_auth(struct idtp9220_device_info *di)
	
{
	
	di->bus.write(di, REG_SSCMND, SEND_DEVICE_AUTH);
	
}
	
	
static ssize_t chip_version_show(struct device *dev,
	
			   struct device_attribute *attr,
	
			   char *buf)
	
{
	
	u8 chip_id_l, chip_id_h, chip_rev, cust_id, status, vset;
	
	u8 fw_otp_ver[4], fw_app_ver[4];
	
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	
	struct idtp9220_device_info *di = i2c_get_clientdata(client);
	
	di->bus.read(di, REG_STATUS_L, &status);
	
	di->bus.read(di, REG_VOUT_SET, &vset);
	
	
	di->bus.read(di, REG_CHIP_ID_L, &chip_id_l);
	
	di->bus.read(di, REG_CHIP_ID_H, &chip_id_h);
	
	di->bus.read(di, REG_CHIP_REV, &chip_rev);
	
	chip_rev = chip_rev  >> 4;
	
	di->bus.read(di, REG_CTM_ID, &cust_id);
	
	di->bus.read_buf(di, REG_OTPFWVER_ADDR, fw_otp_ver, 4);
	
	di->bus.read_buf(di, REG_EPRFWVER_ADDR, fw_app_ver, 4);
	
	
	return sprintf(buf, "chip_id_l:%02x\nchip_id_h:%02x\nchip_rev:%02x\ncust_id:%02x status:%02x vset:%02x\n otp_ver:%x.%x.%x.%x\n app_ver:%x.%x.%x.%x\n",
	
			chip_id_l, chip_id_h, chip_rev, cust_id, status, vset,
	
			fw_otp_ver[0], fw_otp_ver[1], fw_otp_ver[2], fw_otp_ver[3],
	
			fw_app_ver[0], fw_app_ver[1], fw_app_ver[2], fw_app_ver[3]);
	
}
	
	
/* voltage limit attrs */
	
static ssize_t chip_vout_show(struct device *dev,
	
			struct device_attribute *attr,
	
			char *buf)
	
{
	
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	
	struct idtp9220_device_info *di = i2c_get_clientdata(client);
	
	int vout;
	
	
	vout = idtp9220_get_vout(di);
	
	
	return sprintf(buf, "Vout ADC Value: %dMV\n", vout);
	
}
	
	
static ssize_t chip_vout_store(struct device *dev,
	
			 struct device_attribute *attr,
	
			 const char *buf,
	
			 size_t count)
	
{
	
	int index;
	
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	
	struct idtp9220_device_info *di = i2c_get_clientdata(client);
	
	
	index = (int)simple_strtoul(buf, NULL, 16);
	
	if ((index < VOUT_VAL_3500_MV) || (index > VOUT_VAL_5000_MV)) {
	
		dev_err(di->dev, "Store Val %s is invalid!\n", buf);
	
		return count;
	
	}
	
	
	di->bus.write(di, REG_VOUT_SET, index);
	
	
	return count;
	
}
	
	
/* current attrs */
	
static ssize_t chip_iout_show(struct device *dev,
	
			struct device_attribute *attr,
	
			char *buf)
	
{
	
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	
	struct idtp9220_device_info *di = i2c_get_clientdata(client);
	
	int cout;
	
	
	cout = idtp9220_get_iout(di);
	
	
	return sprintf(buf, "Output Current: %dMA\n", cout);
	
}
	
	
static ssize_t chip_iout_store(struct device *dev,
	
			 struct device_attribute *attr,
	
			 const char *buf,
	
			 size_t count)
	
{
	
	int index;
	
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	
	struct idtp9220_device_info *di = i2c_get_clientdata(client);
	
	
	index = (int)simple_strtoul(buf, NULL, 10);
	
	
	if ((index < CURR_VAL_100_MA) || (index > CURR_VAL_1300_MA)) {
	
		dev_err(di->dev, "Store Val %s is invalid", buf);
	
		return count;
	
	}
	
	
	di->bus.write(di, REG_ILIM_SET, index);
	
	
	return count;
	
}
	
	
static ssize_t chip_freq_show(struct device *dev,
	
			struct device_attribute *attr,
	
			char *buf)
	
{
	
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	
	struct idtp9220_device_info *di = i2c_get_clientdata(client);
	
	int f;
	
	
	f = idtp9220_get_freq(di);
	
	
	return sprintf(buf, "Output Current: %dkHz\n", f);
	
}
	
	
static void idtp9220_charging_info(struct idtp9220_device_info *di)
	
{
	
	if (!di)
	
		return;
	
	
	idtp9220_get_vout(di);
	
	idtp9220_get_iout(di);
	
	idtp9220_get_freq(di);
	
	idtp9220_get_vrect(di);
	
	
	dev_info(di->dev, "%s:Vout:%dmV,Iout:%dmA,Freq:%dKHz,Vrect:%dmV,SS:%d\n", __func__,
	
			  di->vout, di->iout, di->f, di->vrect, di->ss);
	printk(KERN_ALERT "%s:Vout:%dmV,Iout:%dmA,Freq:%dKHz,Vrect:%dmV,SS:%d\n", __func__,
	
			  di->vout, di->iout, di->f, di->vrect, di->ss);
	
}
	
	
/* chip enable attrs */
	
static ssize_t chip_enable_show(struct device *dev,
	
			struct device_attribute *attr,
	
			char *buf)
	
{
	
	int ret;
	
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	
	struct idtp9220_device_info *di = i2c_get_clientdata(client);
	
	
	if (gpio_is_valid(di->dt_props.enable_gpio))
	
		ret = gpio_get_value(di->dt_props.enable_gpio);
	
	else {
	
		dev_err(di->dev, "%s: sleep gpio not provided\n", __func__);
	
		ret = -1;
	
	}
	
	
	dev_info(di->dev, "chip enable gpio: %d\n", ret);
	printk(KERN_ALERT "chip enable gpio: %d\n", ret);
	
	return sprintf(buf, "Chip enable: %d\n", !ret);
	
}
	
	
static int idtp9220_set_present(struct idtp9220_device_info *di, int enable)
	
{
	
	int ret = 0;
	
	
	dev_info(di->dev, "[idtp] dc plug %s\n", enable ? "in" : "out");
	printk(KERN_ALERT "[idtp] dc plug %s\n", enable ? "in" : "out");
	
	if (enable)
	
	{
	
		di->dcin_present = true;
	
	} else {
	
		di->status = 0;
	
		di->count_9v = 0;
	
		di->count_5v = 0;
	
		di->exchange = 0;
	
		di->dcin_present = false;
	
		idt_signal_range = 2;
	
		di->ss = 2;
	
		cancel_delayed_work(&di->chg_monitor_work);
	
	}
	
	
	return ret;
	
}
	
	
	
static int idtp9220_set_enable_mode(struct idtp9220_device_info *di, int enable)
	
{
	
	int ret = 0;
	
	
	if (gpio_is_valid(di->dt_props.enable_gpio)) {
	
		ret = gpio_request(di->dt_props.enable_gpio,
	
				"idt-enable-gpio");
	
		if (ret) {
	
			dev_err(di->dev,
	
					"%s: unable to request idt enable gpio [%d]\n",
	
					__func__, di->dt_props.enable_gpio);
	
		}
	
	
		ret = gpio_direction_output(di->dt_props.enable_gpio, !enable);
	
		if (ret) {
	
			dev_err(di->dev,
	
					"%s: cannot set direction for idt enable gpio [%d]\n",
	
					__func__, di->dt_props.enable_gpio);
	
		}
	
		gpio_free(di->dt_props.enable_gpio);
	
	}
	
	
	return ret;
	
}
	
	
static ssize_t chip_enable_store(struct device *dev,
	
			 struct device_attribute *attr,
	
			 const char *buf,
	
			 size_t count)
	
{
	
	int ret, enable;
	
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	
	struct idtp9220_device_info *di = i2c_get_clientdata(client);
	
	
	ret = (int)simple_strtoul(buf, NULL, 10);
	
	enable = !!ret;
	
	
	idtp9220_set_enable_mode(di, enable);
	
	
	return count;
	
}
	
	
static DEVICE_ATTR(chip_enable, S_IWUSR | S_IRUGO, chip_enable_show, chip_enable_store);
	
static DEVICE_ATTR(chip_version, S_IRUGO, chip_version_show, NULL);
	
static DEVICE_ATTR(chip_vout, S_IWUSR | S_IRUGO, chip_vout_show, chip_vout_store);
	
static DEVICE_ATTR(chip_iout, S_IWUSR | S_IRUGO, chip_iout_show, chip_iout_store);
	
static DEVICE_ATTR(chip_freq, S_IRUGO, chip_freq_show, NULL);
	
	
static struct attribute *sysfs_attrs[] = {
	
	&dev_attr_chip_version.attr,
	
	&dev_attr_chip_vout.attr,
	
	&dev_attr_chip_iout.attr,
	
	&dev_attr_chip_freq.attr,
	
	&dev_attr_chip_enable.attr,
	
	NULL,
	
};
	
	
static const struct attribute_group sysfs_group_attrs = {
	
	.attrs = sysfs_attrs,
	
};
	
	
	
static int idtp9220_parse_dt(struct idtp9220_device_info *di)
	
{
	
	/*struct device_node *node = di->dev->of_node;
	
	
	if (!node) {
	
		dev_err(di->dev, "device tree node missing\n");
	
		return -EINVAL;
	
	}
	
	
	di->dt_props.irq_gpio = of_get_named_gpio(node, "idt,irq", 0);
	
	if ((!gpio_is_valid(di->dt_props.irq_gpio)))
	
		return -EINVAL;
	
	
	di->dt_props.enable_gpio = of_get_named_gpio(node, "idt,enable", 0);
	
	if ((!gpio_is_valid(di->dt_props.enable_gpio)))
	
		return -EINVAL;
	*/

	di->dt_props.irq_gpio = GPIO63;
	di->dt_props.enable_gpio = GPIO138;

	
	return 0;
	
}
	
	
static int idtp9220_gpio_init(struct idtp9220_device_info *di)
	
{
	
	int ret = 0;
	
	int irqn = 0;
	
	
	di->idt_pinctrl = devm_pinctrl_get(di->dev);
	
	if (IS_ERR_OR_NULL(di->idt_pinctrl)) {
	
		dev_err(di->dev, "No pinctrl config specified\n");
	
		ret = PTR_ERR(di->dev);
	
		return ret;
	
	}
	
	di->idt_gpio_active =
	
		pinctrl_lookup_state(di->idt_pinctrl, "idt_active");
	
	if (IS_ERR_OR_NULL(di->idt_gpio_active)) {
	
		dev_err(di->dev, "No active config specified\n");
	
		ret = PTR_ERR(di->idt_gpio_active);
	
		return ret;
	
	}
	
	di->idt_gpio_suspend =
	
		pinctrl_lookup_state(di->idt_pinctrl, "idt_suspend");
	
	if (IS_ERR_OR_NULL(di->idt_gpio_suspend)) {
	
		dev_err(di->dev, "No suspend config specified\n");
	
		ret = PTR_ERR(di->idt_gpio_suspend);
	
		return ret;
	
	}
	
	
	ret = pinctrl_select_state(di->idt_pinctrl,
	
			di->idt_gpio_active);
	
	if (ret < 0) {
	
		dev_err(di->dev, "fail to select pinctrl active rc=%d\n",
	
			ret);
	
		return ret;
	
	}
	
	
	if (gpio_is_valid(di->dt_props.irq_gpio)) {
	
		irqn = gpio_to_irq(di->dt_props.irq_gpio);
	
		if (irqn < 0) {
	
			ret = irqn;
	
			goto err_irq_gpio;
	
		}
	
		di->irq = irqn;
	
	} else {
	
		dev_err(di->dev, "%s: irq gpio not provided\n", __func__);
	
		goto err_irq_gpio;
	
	}
	
	
err_irq_gpio:
	
	gpio_free(di->dt_props.irq_gpio);
	
	return ret;
	
}
	
	
static bool need_irq_cleared(struct idtp9220_device_info *di)
	
{
	
	u8 int_buf[2];
	
	u16 int_val;
	
	int rc = -1;
	
	
	rc = di->bus.read_buf(di, REG_INTR_L, int_buf, 2);
	
	if (rc < 0) {
	
		dev_err(di->dev, "%s: read int state error\n", __func__);
	
		return true;
	
	}
	
	int_val = int_buf[0] | (int_buf[1] << 8);
	
	if (int_val != 0) {
	
		dev_info(di->dev, "irq not clear right: 0x%04x\n", int_val);
		printk(KERN_ALERT "irq not clear right: 0x%04x\n", int_val);
	
		return true;
	
	}
	
	
	if (gpio_is_valid(di->dt_props.irq_gpio))
	
		rc = gpio_get_value(di->dt_props.irq_gpio);
	
	else {
	
		dev_err(di->dev, "%s: irq gpio not provided\n", __func__);
		printk(KERN_ALERT "%s: irq gpio not provided\n", __func__);
	
		rc = -1;
	
	}
	
	if (!rc) {
	
		dev_info(di->dev, "irq low, need clear int: %d\n", rc);
		printk(KERN_ALERT "irq low, need clear int: %d\n", rc);
	
		return true;
	
	}
	
	return false;
	
}
	
	
#define ADAPTER_QC_VOL		9500
	
#define ADAPTER_DEFAULT_VOL	5200
	
#define CHARGING_PERIOD_S	10
	
#define TAPER_CURR_Limit	950000
	
	
static void idtp9220_monitor_work(struct work_struct *work)
	
{
	
	struct idtp9220_device_info *di =
	
			container_of(work, struct idtp9220_device_info,
	
			chg_monitor_work.work);
	
	
	idtp9220_set_charging_param(di);
	
	idtp9220_charging_info(di);
	
	
	schedule_delayed_work(&di->chg_monitor_work,
	
						CHARGING_PERIOD_S * HZ);
	
	
}
	
	
static void idtp9220_chg_detect_work(struct work_struct *work)
	
{
	
	struct idtp9220_device_info *di =
	
			container_of(work, struct idtp9220_device_info,
	
			chg_detect_work.work);
	
	
	union power_supply_propval val = {0, };
	
	
	dev_info(di->dev, "[idt] enter %s\n", __func__);
	
	
	/*set idtp9220 into sleep mode when usbin*/
	
	power_supply_get_property(di->usb_psy,
	
			POWER_SUPPLY_PROP_PRESENT, &val);
	
	if (val.intval) {
	
		dev_info(di->dev, "[idt] usb_online:%d set chip disable\n", val.intval);
	
		idtp9220_set_enable_mode(di, false);
	
		return;
	
	}
	
	
	if (di->dc_psy) {
	
		power_supply_get_property(di->dc_psy,
	
			POWER_SUPPLY_PROP_ONLINE, &val);
	
		dev_info(di->dev, "[idt] dc_online %d\n", val.intval);
	
		if (val.intval) {
	
			schedule_delayed_work(&di->irq_work,
	
				msecs_to_jiffies(30));
	
		}
	
	}
	
}
	
	
static void idtp922x_set_test_reg(struct idtp9220_device_info *di)
	
{
	
	
	u8 fw_app_ver[4] = {0};
	
	
	di->bus.read_buf(di, REG_EPRFWVER_ADDR, fw_app_ver, 4);
	
	dev_info(di->dev, "[idt] %s:fw ver %d\n", __func__, fw_app_ver[0]);
	
	if (fw_app_ver[0] <= 9 &&
	
		fw_app_ver[1] == 2 &&
	
		fw_app_ver[2] == 1 &&
	
		fw_app_ver[3] == 2) {
	
	
		di->bus.write(di, 0x68, 0xAE);
	
		di->bus.write(di, 0x69, 0x1E);
	
		di->bus.write(di, 0x6A, 0xF0);
	
		di->bus.write(di, 0x6B, 0x70);
	
		di->bus.write(di, 0x6C, 0xF0);
	
		di->bus.write(di, 0x6D, 0x70);
	
		di->bus.write(di, 0x6E, 0xB0);
	
		di->bus.write(di, 0x6F, 0x50);
	
		di->bus.write(di, 0x70, 0xC0);
	
		di->bus.write(di, 0x71, 0x70);
	
	}
	
	
	di->bus.write(di, 0x72, 0x82);
	
	di->bus.write(di, 0x73, 0x32);
	
}
	
	
#define DC_FUL_CURRENT 100000
	
#define DC_LOW_CURRENT 300000
	
#define DC_SDP_CURRENT 500000
	
#define DC_DCP_CURRENT 950000
	
#define DC_QC2_CURRENT 700000
	
#define DC_QC3_CURRENT 800000
	
#define ICL_EXCHANGE_CURRENT 600000
	
#define ICL_EXCHANGE_COUNT   5 /*5 = 1min*/
	
#define EXCHANGE_9V          0x0
	
#define EXCHANGE_5V          0x1
	
#define TAPER_SOC 95
	
#define FULL_SOC 100
	
#define TAPER_VOL 4350000
	
#define TAPER_CUR -500000
	
#define BOOST_MODE  0x1
	
#define NORMAL_MODE 0x2
	
#define TAPER_MODE  0x4
	
#define FULL_MODE   0x8
	
#define BAD_HEALTH  0x10
	
	
static void idtp9220_set_charging_param(struct idtp9220_device_info *di)
	
{
	
	int soc = 0, health = 0, batt_sts = 0;
	
	int adapter_vol = 0, icl_curr = 0;
	
	int cur_now = 0, vol_now = 0;
	
	union power_supply_propval val = {0, };
	
	int input_now = 0;
	
	
	switch(di->tx_charger_type) {
	
		case ADAPTER_QC2:
	
			adapter_vol = ADAPTER_QC_VOL;
	
			icl_curr = DC_QC2_CURRENT;
	
			di->status |= BOOST_MODE;
	
			break;
	
		case ADAPTER_QC3:
	
			adapter_vol = ADAPTER_QC_VOL;
	
			icl_curr = DC_QC3_CURRENT;
	
			di->status |= BOOST_MODE;
	
			break;
	
		case ADAPTER_DCP:
	
		case ADAPTER_CDP:
	
		case ADAPTER_PD:
	
		case ADAPTER_AUTH_FAILED:
	
			adapter_vol = ADAPTER_DEFAULT_VOL;
	
			icl_curr = DC_DCP_CURRENT;
	
			idtp9220_set_vout(di, VOUT_VAL_5200_MV);
	
			di->status |= NORMAL_MODE;
	
			break;
	
		case ADAPTER_SDP:
	
			adapter_vol = ADAPTER_DEFAULT_VOL;
	
			icl_curr = DC_LOW_CURRENT;
	
			idtp9220_set_vout(di, VOUT_VAL_5200_MV);
	
			break;
	
		default:
	
			adapter_vol = ADAPTER_DEFAULT_VOL;
	
			icl_curr = 0;
	
			break;
	
	}
	
	
	power_supply_get_property(di->batt_psy,
	
			POWER_SUPPLY_PROP_STATUS, &val);
	
	batt_sts = val.intval;
	
	
	/*if battery vol > 4350 && soc > 95 && ichg < -800mA set adapter to 5V*/
	
	power_supply_get_property(di->batt_psy,
	
			POWER_SUPPLY_PROP_CAPACITY, &val);
	
	soc = val.intval;
	
	
	power_supply_get_property(di->batt_psy,
	
			POWER_SUPPLY_PROP_VOLTAGE_NOW, &val);
	
	vol_now = val.intval;
	
	
	power_supply_get_property(di->batt_psy,
	
			POWER_SUPPLY_PROP_CURRENT_NOW, &val);
	
	cur_now = val.intval;
	
	
	power_supply_get_property(di->dc_psy, POWER_SUPPLY_PROP_INPUT_CURRENT_NOW, &val);
	
	input_now = val.intval;
	
	
	power_supply_get_property(di->batt_psy,
	
			POWER_SUPPLY_PROP_HEALTH, &val);
	
	health = val.intval;
	
	
	if (health != POWER_SUPPLY_HEALTH_GOOD)
	
		di->status |= BAD_HEALTH;
	
	else
	
		di->status &= ~BAD_HEALTH;
	
	
	/*set to 5.2V 900mA*/
	
	if (di->status & BOOST_MODE) {
	
		if (input_now < ICL_EXCHANGE_CURRENT && di->exchange == EXCHANGE_9V)
	
		{
	
			di->count_5v++;
	
			di->count_9v = 0;
	
		} else if (input_now > ICL_EXCHANGE_CURRENT && di->exchange == EXCHANGE_5V) {
	
			di->count_9v++;
	
			di->count_5v = 0;
	
		} else {
	
			di->count_5v = 0;
	
			di->count_9v = 0;
	
		}
	
		/*
	
		 * 9V-->5V check 6 times
	
		 * 5V-->9v check 3 times
	
		 */
	
		if (di->count_5v > ICL_EXCHANGE_COUNT ||
	
				(di->exchange == EXCHANGE_5V && di->count_9v <= ICL_EXCHANGE_COUNT - 3))
	
		{
	
			adapter_vol = ADAPTER_DEFAULT_VOL;
	
			icl_curr = DC_DCP_CURRENT;
	
			di->exchange = EXCHANGE_5V;
	
		} else if (di->count_9v > (ICL_EXCHANGE_COUNT - 3))
	
			di->exchange = EXCHANGE_9V;
	
	
		/*health not good set adapter vbus to 5V*/
	
		if (di->status & BAD_HEALTH)
	
		{
	
			adapter_vol = ADAPTER_DEFAULT_VOL;
	
		}
	
	}
	
	
	/*set to 5.2V 500mA*/
	
	if ((soc >= TAPER_SOC && vol_now >= TAPER_VOL && cur_now > TAPER_CUR)
	
			|| (di->status & TAPER_MODE))
	
	{
	
		adapter_vol = ADAPTER_DEFAULT_VOL;
	
		icl_curr = min(DC_SDP_CURRENT, icl_curr);
	
		di->status |= TAPER_MODE;
	
	}
	
	if (soc < TAPER_SOC - 1 && di->status & TAPER_MODE)
	
		di->status &= ~TAPER_MODE;
	
	
	/*if battery status is full set to 5.2V 100mA*/
	
	if ((soc == FULL_SOC && batt_sts == POWER_SUPPLY_STATUS_FULL)
	
			|| (di->status & FULL_MODE))
	
	{
	
		adapter_vol = ADAPTER_DEFAULT_VOL;
	
		icl_curr = DC_FUL_CURRENT;
	
		di->status |= FULL_MODE;
	
	}
	
	if ((soc < FULL_SOC - 1) && (di->status & FULL_MODE)) {
	
		di->status &= ~FULL_MODE;
	
		adapter_vol = ADAPTER_DEFAULT_VOL;
	
		icl_curr = DC_LOW_CURRENT;
	
	}
	
	printk("[idtp] soc:%d,vol_now:%d,cur_now:%d,input_now:%d,health:%d\n",
	
			soc, vol_now, cur_now, input_now, health);
	
	printk("[idtp] di->status:0x%x,adapter_vol =%d,icl_curr=%d\n",
	
			di->status, adapter_vol, icl_curr);
	
	
	idtp922x_set_adap_vol(di, adapter_vol);
	
	if (adapter_vol == ADAPTER_QC_VOL)
	
		idtp922x_set_test_reg(di);
	
	idtp922x_set_pmi_icl(di, icl_curr);
	
}
	
	
static void idtp9220_irq_work(struct work_struct *work)
	
{
	
	struct idtp9220_device_info *di =
	
			container_of(work, struct idtp9220_device_info,
	
			irq_work.work);
	
	u8 int_buf[2] = {0};
	
	u16 int_val = 0;
	
	int rc = 0;
	
	u8 recive_data[4] = {0};
	
	static int retry;
	
	static int retry_count;
	
	
	rc = di->bus.read_buf(di, REG_INTR_L, int_buf, 2);
	
	if (rc < 0) {
	
		dev_err(di->dev, "[idt]read int state error: %d\n", rc);
	
		goto out;
	
	}
	
	/* clear int and enable irq immediately when read int register*/
	
	idtp922x_clrInt(di, int_buf, 2);
	
	
	int_val = int_buf[0] | (int_buf[1] << 8);
	
	dev_info(di->dev, "[idt] int: 0x%04x\n", int_val);
	
	
	if (int_val & INT_IDAUTH_SUCESS) {
	
		idtp9220_send_device_auth(di);
	
		goto out;
	
	}
	
	
	if (int_val & INT_AUTH_SUCESS) {
	
		idtp922x_request_adapter(di);
	
		goto out;
	
	}
	
	idtp9220_get_signal_strength(di);
	
	
	if ((int_val & INT_IDAUTH_FAIL) || (int_val & INT_AUTH_FAIL)) {
	
		if ((int_val & INT_AUTH_FAIL) && (retry < 5)) {
	
			idtp9220_send_device_auth(di);
	
			retry++;
	
			dev_info(di->dev, "[idtp] dev auth failed retry %d\n", retry);
	
			goto out;
	
		} else
	
			retry = 0;
	
		di->tx_charger_type = ADAPTER_AUTH_FAILED;
	
		dev_info(di->dev, "[idtp] auth failed tx charger type set %d\n", di->tx_charger_type);
	
		schedule_delayed_work(&di->chg_monitor_work,
	
							msecs_to_jiffies(0));
	
		goto out;
	
	} else
	
		retry = 0;
	
	
	
	if (int_val & INT_SEND_TIMEOUT) {
	
		if (retry_count < 3) {
	
			dev_info(di->dev, "timeout retry %d\n", retry_count);
	
			idtp922x_request_adapter(di);
	
			retry_count++;
	
			goto out;
	
		} else {
	
			dev_err(di->dev, "%s: retry failed\n", __func__);
	
			retry_count = 0;
	
			goto out;
	
		}
	
	} else {
	
		retry_count = 0;
	
	
	}
	
	
	if (int_val & INT_TX_DATA_RECV) {
	
		idtp922x_receivePkt(di, recive_data);
	
		dev_info(di->dev, "[idt] cmd: %x\n", recive_data[0]);
	
	
		switch (recive_data[0]) {
	
		case BC_ADAPTER_TYPE:
	
			dev_info(di->dev, "[idt]adapter type: %d\n", recive_data[1]);
	
			di->tx_charger_type = recive_data[1];
	
			schedule_delayed_work(&di->chg_monitor_work,
	
								msecs_to_jiffies(0));
	
			break;
	
		default:
	
			dev_info(di->dev, "[idt] unsupport cmd: %x\n", recive_data[0]);
	
			break;
	
		}
	
	}
	
	
out:
	
	if (need_irq_cleared(di))
	
	{
	
		u8 clr_buf[2] = {0xFF, 0xFF};
	
		idtp922x_clrInt(di, clr_buf, 2);
	
	}
	
	return;
	
}
	
static void idtp9220_get_property_names(struct idtp9220_device_info *di)
	
{
	
	di->batt_psy = power_supply_get_by_name("battery");
	
	if (!di->batt_psy) {
	
		dev_err(di->dev, "[idt] no batt_psy,return\n");
	
		return;
	
	}
	
	di->dc_psy = power_supply_get_by_name("dc");
	
	if (!di->dc_psy) {
	
		dev_err(di->dev, "[idt] no dc_psy,return\n");
	
		return;
	
	}
	
	di->usb_psy = power_supply_get_by_name("usb");
	
	if (!di->usb_psy) {
	
		dev_err(di->dev, "[idt] no usb_psy,return\n");
	
		return;
	
	}
	
}
	
	
static irqreturn_t idtp9220_irq_handler(int irq, void *dev_id)
	
{
	
	
	struct idtp9220_device_info *di = dev_id;
	
	
	schedule_delayed_work(&di->irq_work, msecs_to_jiffies(30));
	
	
	return IRQ_HANDLED;
	
}
	
	
static int idtp9220_irq_request(struct idtp9220_device_info *di)
	
{
	
	int ret = 0;
	
	
	if (!di->irq) {
	
		dev_err(di->dev, "%s: irq is wrong\n", __func__);
	
		return -EINVAL;
	
	}
	
	
	ret = request_irq(di->irq, idtp9220_irq_handler,
	
			  IRQF_TRIGGER_FALLING, di->name, di);
	
	if (ret) {
	
		dev_err(di->dev, "%s: request_irq failed\n", __func__);
	
		return ret;
	
	}
	
	
	return 0;
	
}
	
	
static struct regmap_config i2c_idtp9220_regmap_config = {
	
	.reg_bits  = 16,
	
	.val_bits  = 8,
	
	.max_register  = 0xFFFF,
	
};
	
	
static int idtp9220_get_version(struct idtp9220_device_info *di)
	
{
	
	int id_val = 0;
	
	u8 chip_id[2] = {0};
	
	
	di->bus.read_buf(di, REG_CHIP_ID_L, chip_id, 2);
	
	id_val = (chip_id[1] << 8) | chip_id[0];
	
	
	return id_val;
	
}
	
	
	
static enum power_supply_property idtp9220_props[] = {
	
	POWER_SUPPLY_PROP_PIN_ENABLED,
	
	POWER_SUPPLY_PROP_PRESENT,
	
	POWER_SUPPLY_PROP_WIRELESS_VERSION,
	
	POWER_SUPPLY_PROP_SIGNAL_STRENGTH,
	
};
	
	
static int idtp9220_get_prop(struct power_supply *psy,
	
		enum power_supply_property psp,
	
		union power_supply_propval *val)
	
{
	
	struct idtp9220_device_info *di = power_supply_get_drvdata(psy);
	
	
	switch (psp) {
	
	case POWER_SUPPLY_PROP_PIN_ENABLED:
	
		val->intval = gpio_get_value(di->dt_props.enable_gpio);
	
		break;
	
	case POWER_SUPPLY_PROP_PRESENT:
	
		val->intval = di->dcin_present;
	
		break;
	
	case POWER_SUPPLY_PROP_WIRELESS_VERSION:
	
		val->intval = idtp9220_get_version(di);
	
		break;
	
	case POWER_SUPPLY_PROP_SIGNAL_STRENGTH:
	
		val->intval = di->ss;
	
		break;
	
	default:
	
		return -EINVAL;
	
	}
	
	return 0;
	
}
	
	
static int idtp9220_set_prop(struct power_supply *psy,
	
		enum power_supply_property psp,
	
		const union power_supply_propval *val)
	
{
	
	struct idtp9220_device_info *di = power_supply_get_drvdata(psy);
	
	int rc = 0;
	
	
	switch (psp) {
	
	case POWER_SUPPLY_PROP_PIN_ENABLED:
	
		rc = idtp9220_set_enable_mode(di, val->intval);
	
		break;
	
	case POWER_SUPPLY_PROP_PRESENT:
	
		rc = idtp9220_set_present(di, val->intval);
	
	default:
	
		return -EINVAL;
	
	}
	
	
	return rc;
	
}
	
	
static int idtp9220_prop_is_writeable(struct power_supply *psy,
	
		enum power_supply_property psp)
	
{
	
	int rc;
	
	
	switch (psp) {
	
	case POWER_SUPPLY_PROP_PIN_ENABLED:
	
	case POWER_SUPPLY_PROP_PRESENT:
	
		rc = 1;
	
		break;
	
	default:
	
		rc = 0;
	
		break;
	
	}
	
	
	return rc;
	
}
	
	
static const struct power_supply_desc idtp_psy_desc = {
	
	.name = "idt",
	
	.type = POWER_SUPPLY_TYPE_WIRELESS,
	
	.properties = idtp9220_props,
	
	.num_properties = ARRAY_SIZE(idtp9220_props),
	
	.get_property = idtp9220_get_prop,
	
	.set_property = idtp9220_set_prop,
	
	.property_is_writeable = idtp9220_prop_is_writeable,
	
};
	
	
#ifdef IDTP9220_SRAM_UPDATE
	
static void idtp9220_sram_update_work(struct work_struct *work)
	
{
	
	struct idtp9220_device_info *di =
	
		container_of(work, struct idtp9220_device_info,
	
				sram_update_work.work);
	
	u8 data;
	
	int size = sizeof(idt_firmware_sram);
	
	u8 buffer[size];
	
	int i = 0;
	
	
	
	di->bus.read(di, 0x4D, &data);
	
	dev_info(di->dev, "[idtp] %s: 0x4D data:%x, (data & BIT(4)):%lu\n", __func__, data, (data & BIT(4)));
	
	if (!(data & BIT(4)))
	
		return;
	
	
	di->bus.write_buf(di, 0x0600, idt_firmware_sram, size);
	
	di->bus.read_buf(di, 0x0600, buffer, size);
	
	
	while(size--)
	
	{
	
		if (idt_firmware_sram[i] == buffer[i])
	
		{
	
			printk("buffer[%d]:0x%x", i, buffer[i]);
	
		} else
	
		{
	
			printk("[idt] sram data is not right\n");
	
			return;
	
		}
	
		i++;
	
	}
	
	
	di->bus.write(di, 0x4F, 0x5A);
	
	
	di->bus.write(di, 0x4E, BIT(6));
	
	
	mdelay(1000);
	
	di->bus.read(di, 0x4D, &data);
	
	dev_info(di->dev, "[idtp] %s: 0x4D data:%x\n", __func__, data);
	
}
	
#endif
	

static struct i2c_board_info __initdata i2c_idtp9220_boardinfo =
{
	I2C_BOARD_INFO((IDT_DRIVER_NAME), (DEVICE_ADDR))
};
	
	
static int idtp9220_probe(struct i2c_client *client,
	
				 const struct i2c_device_id *id)
	
{
	
	int ret = 0;
	
	struct idtp9220_device_info *di;


	
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
	
	struct power_supply_config idtp_cfg = {};

	//Luxios
	client->addr = 0x61;
	i2c_register_board_info(DEVICE_BUS, &i2c_idtp9220_boardinfo, 1);
	
	printk(KERN_ALERT "idtp9220: probe started \n");
	
	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE)) {
	
		dev_err(&client->dev, "i2c check functionality failed!\n");
		printk(KERN_ALERT "i2c check functionality failed!\n");
	
		return -EIO;
	
	}
	
	
	di = devm_kzalloc(&client->dev, sizeof(*di), GFP_KERNEL);
	
	if (!di) {
	
		dev_err(&client->dev, "i2c allocated device info data failed!\n");
		printk(KERN_ALERT "i2c allocated device info data failed!\n");
	
		return -ENOMEM;
	
	}
	
	
	di->name = IDT_DRIVER_NAME;
	
	di->dev = &client->dev;
	
	di->chip_enable = 1;
	
	di->ss = 2;
	
	di->regmap = devm_regmap_init_i2c(client, &i2c_idtp9220_regmap_config);
	
	if (!di->regmap)
	
		return -ENODEV;
	
	di->bus.read = idtp9220_read;
	
	di->bus.write = idtp9220_write;
	
	di->bus.read_buf = idtp9220_read_buffer;
	
	di->bus.write_buf = idtp9220_write_buffer;
	
	INIT_DELAYED_WORK(&di->irq_work, idtp9220_irq_work);
	
	mutex_init(&di->read_lock);
	
	mutex_init(&di->write_lock);
	
	device_init_wakeup(&client->dev, true);
	
	i2c_set_clientdata(client, di);
	
	g_di = di;
	
	
	ret = idtp9220_parse_dt(di);
	
	if (ret < 0) {
	
		dev_err(di->dev, "%s: parse dt error [%d]\n",
	
				__func__, ret);
		printk(KERN_ALERT "%s: parse dt error [%d]\n",
	
				__func__, ret);
	
		goto cleanup;
	
	}
	
	
	ret = idtp9220_gpio_init(di);
	
	if (ret < 0) {
	
		dev_err(di->dev, "%s: gpio init error [%d]\n",
	
				__func__, ret);
		printk(KERN_ALERT "%s: gpio init error [%d]\n",
	
				__func__, ret);
	
		goto cleanup;
	
	}
	
	
	ret = idtp9220_irq_request(di);
	
	if (ret < 0) {
	
		dev_err(di->dev, "%s: request irq error [%d]\n",
	
				__func__, ret);
		printk(KERN_ALERT "%s: request irq error [%d]\n",
	
				__func__, ret);
	
		goto cleanup;
	
	}
	
	
/*
	
 *	this func write config to otp when init, due to the config will be
	
 *	write to otp in factory, so delete it.
	
 *
	
	
	if (!program_fw(di, 0x0000, idt_firmware_otp, sizeof(idt_firmware_otp))) {
	
		dev_err(&client->dev, "program fw failed.\n");
	
		goto cleanup;
	
	}
	
*/
	
	
	if (sysfs_create_group(&client->dev.kobj, &sysfs_group_attrs)) {
	
		dev_err(&client->dev, "create sysfs attrs failed!\n");
		printk(KERN_ALERT " idtp9220 create sysfs attrs failed!\n");
	
		ret = -EIO;
	
		goto cleanup;
	
	}
	
	idtp9220_get_property_names(di);
	
	idtp_cfg.drv_data = di;
	
	di->idtp_psy = power_supply_register(di->dev,
	
			 &idtp_psy_desc,
	
			 &idtp_cfg);
	
	
	INIT_DELAYED_WORK(&di->chg_monitor_work,idtp9220_monitor_work);
	
	INIT_DELAYED_WORK(&di->chg_detect_work,idtp9220_chg_detect_work);
	
	
	dev_info(di->dev, "[idt] success probe idtp922x driver\n");
	printk(KERN_ALERT "[idtp9220] success probe idtp922x driver\n");
	
	schedule_delayed_work(&di->chg_detect_work, 5 * HZ);
	
	
#ifdef IDTP9220_SRAM_UPDATE
	
	INIT_DELAYED_WORK(&di->sram_update_work,idtp9220_sram_update_work);
	
	schedule_delayed_work(&di->sram_update_work, 10 * HZ);
	
#endif
	
	return 0;
	
	
cleanup:
	
	free_irq(di->irq, di);
	
	cancel_delayed_work_sync(&di->irq_work);
	
	i2c_set_clientdata(client, NULL);
	
	
	return ret;
	
}
	
	
static int idtp9220_remove(struct i2c_client *client)
	
{
	
	struct idtp9220_device_info *di = i2c_get_clientdata(client);
	
	
	gpio_free(di->dt_props.enable_gpio);
	
	cancel_delayed_work_sync(&di->irq_work);
	
	i2c_set_clientdata(client, NULL);
	
	
	return 0;
	
}
	
	
static const struct i2c_device_id idtp9220_id[] = {
	
	{IDT_DRIVER_NAME, 0},
	
	{},
	
};
	
	
static const struct of_device_id idt_match_table[] = {
	
	{.compatible = "idt,p9220"},
	
	{}
	
};
	
	
MODULE_DEVICE_TABLE(i2c, idtp9220_id);
	
	
static struct i2c_driver idtp9220_driver = {
	
	.driver = {
	
		.name = IDT_DRIVER_NAME,
	
		.owner = THIS_MODULE,
		//Luxios
		//.of_match_table = idt_match_table,
	
	},
	
	.probe = idtp9220_probe,
	
	.remove = idtp9220_remove,
	
	.id_table = idtp9220_id,
	
};

static int __init idtp9220_init(void)
{
	if(i2c_add_driver(&idtp9220_driver)!=0)
	{
		printk(KERN_ALERT "idtp9220 Error unable to add i2c driver.\n");
		return -1;
	}
	return 0;
}

static void __exit idtp9220_exit(void)
{
	//struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	//idtp9220_remove(client);
	return;
}

	
	
//module_i2c_driver(idtp9220_driver);

module_init(idtp9220_init);
module_exit(idtp9220_exit);
	
	
MODULE_AUTHOR("bsp@mobvoi.com");
	
MODULE_DESCRIPTION("IDTP9220 Wireless Power Charger Monitor driver");
	
MODULE_LICENSE("GPL v2");
	

