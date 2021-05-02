// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * HP WMI hotkeys
 *
 * Copyright (C) 2008 Red Hat <mjg@redhat.com>
 * Copyright (C) 2010, 2011 Anssi Hannula <anssi.hannula@iki.fi>
 *
 * Portions based on wistron_btns.c:
 * Copyright (C) 2005 Miloslav Trmac <mitr@volny.cz>
 * Copyright (C) 2005 Bernhard Rosenkraenzer <bero@arklinux.org>
 * Copyright (C) 2005 Dmitry Torokhov <dtor@mail.ru>
 *
 * Portions based on alienware-wmi.c:
 * Copyright (C) 2014 Dell Inc <mario_limonciello@dell.com>
*/

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/input.h>
#include <linux/input/sparse-keymap.h>
#include <linux/platform_device.h>
#include <linux/acpi.h>
#include <linux/rfkill.h>
#include <linux/string.h>

#ifdef STUPID_INTELLISENSE_HACK
#define pr_err(...)
#define pr_warn(...)
#define pr_info(...)
#define pr_debug(...)
#endif

MODULE_AUTHOR("Matthew Garrett <mjg59@srcf.ucam.org>");
MODULE_DESCRIPTION("HP laptop WMI hotkeys driver");
MODULE_LICENSE("GPL");

MODULE_ALIAS("wmi:95F24279-4D7B-4334-9387-ACCDC67EF61C");
MODULE_ALIAS("wmi:5FB7F034-2C63-45e9-BE91-3D44E2C707E4");

#define HPWMI_EVENT_GUID "95F24279-4D7B-4334-9387-ACCDC67EF61C"
#define HPWMI_BIOS_GUID "5FB7F034-2C63-45e9-BE91-3D44E2C707E4"

enum hp_wmi_radio {
  HPWMI_WIFI	= 0x0,
  HPWMI_BLUETOOTH	= 0x1,
  HPWMI_WWAN	= 0x2,
  HPWMI_GPS	= 0x3,
};

enum hp_wmi_event_ids {
  HPWMI_DOCK_EVENT		= 0x01,
  HPWMI_PARK_HDD			= 0x02,
  HPWMI_SMART_ADAPTER		= 0x03,
  HPWMI_BEZEL_BUTTON		= 0x04,
  HPWMI_WIRELESS			= 0x05,
  HPWMI_CPU_BATTERY_THROTTLE	= 0x06,
  HPWMI_LOCK_SWITCH		= 0x07,
  HPWMI_LID_SWITCH		= 0x08,
  HPWMI_SCREEN_ROTATION		= 0x09,
  HPWMI_COOLSENSE_SYSTEM_MOBILE	= 0x0A,
  HPWMI_COOLSENSE_SYSTEM_HOT	= 0x0B,
  HPWMI_PROXIMITY_SENSOR		= 0x0C,
  HPWMI_BACKLIT_KB_BRIGHTNESS	= 0x0D,
  HPWMI_PEAKSHIFT_PERIOD		= 0x0F,
  HPWMI_BATTERY_CHARGE_PERIOD	= 0x10,
  HPWMI_OMEN_KEY      = 0x1D
};

struct bios_args {
  u32 signature;
  u32 command;
  u32 commandtype;
  u32 datasize;
  u8  data[128];
};

enum hp_wmi_commandtype {
  HPWMI_DISPLAY_QUERY		= 0x01,
  HPWMI_HDDTEMP_QUERY		= 0x02,
  HPWMI_ALS_QUERY			= 0x03,
  HPWMI_HARDWARE_QUERY		= 0x04,
  HPWMI_WIRELESS_QUERY		= 0x05,
  HPWMI_BATTERY_QUERY		= 0x07,
  HPWMI_BIOS_QUERY		= 0x09,
  HPWMI_FEATURE_QUERY		= 0x0b,
  HPWMI_HOTKEY_QUERY		= 0x0c,
  HPWMI_FEATURE2_QUERY		= 0x0d,
  HPWMI_WIRELESS2_QUERY		= 0x1b,
  HPWMI_POSTCODEERROR_QUERY	= 0x2a,

  HPWMI_FOURZONE_COLOR_GET = 2,
  HPWMI_FOURZONE_COLOR_SET = 3,
  HPWMI_FOURZONE_BRIGHT_GET = 4,
  HPWMI_FOURZONE_BRIGHT_SET = 5,
  HPWMI_FOURZONE_ANIM_GET = 6,
  HPWMI_FOURZONE_ANIM_SET = 7,
};

enum hp_wmi_command {
  HPWMI_READ	= 0x01,
  HPWMI_WRITE	= 0x02,
  HPWMI_ODM	= 0x03,
  HPWMI_FOURZONE = 131081,
};

enum hp_wmi_hardware_mask {
  HPWMI_DOCK_MASK		= 0x01,
  HPWMI_TABLET_MASK	= 0x04,
};

struct bios_return {
  u32 sigpass;
  u32 return_code;
};

enum hp_return_value {
  HPWMI_RET_WRONG_SIGNATURE	= 0x02,
  HPWMI_RET_UNKNOWN_COMMAND	= 0x03,
  HPWMI_RET_UNKNOWN_CMDTYPE	= 0x04,
  HPWMI_RET_INPUT_SIZE_NULL	= 0x05,
  HPWMI_RET_INPUT_DATA_NULL = 0x06,
  HPWMI_RET_INPUT_DATA_INVALID = 0x07,
  HPWMI_RET_RETURN_SIZE_NULL	= 0x08,
  HPWMI_RET_RETURN_SIZE_INVALID = 0x09,
};

enum hp_wireless2_bits {
  HPWMI_POWER_STATE	= 0x01,
  HPWMI_POWER_SOFT	= 0x02,
  HPWMI_POWER_BIOS	= 0x04,
  HPWMI_POWER_HARD	= 0x08,
};

#define IS_HWBLOCKED(x) ((x & (HPWMI_POWER_BIOS | HPWMI_POWER_HARD)) \
       != (HPWMI_POWER_BIOS | HPWMI_POWER_HARD))
#define IS_SWBLOCKED(x) !(x & HPWMI_POWER_SOFT)

struct bios_rfkill2_device_state {
  u8 radio_type;
  u8 bus_type;
  u16 vendor_id;
  u16 product_id;
  u16 subsys_vendor_id;
  u16 subsys_product_id;
  u8 rfkill_id;
  u8 power;
  u8 unknown[4];
};

/* 7 devices fit into the 128 byte buffer */
#define HPWMI_MAX_RFKILL2_DEVICES	7

struct bios_rfkill2_state {
  u8 unknown[7];
  u8 count;
  u8 pad[8];
  struct bios_rfkill2_device_state device[HPWMI_MAX_RFKILL2_DEVICES];
};

static const struct key_entry hp_wmi_keymap[] = {
  { KE_KEY, 0x02,   { KEY_BRIGHTNESSUP } },
  { KE_KEY, 0x03,   { KEY_BRIGHTNESSDOWN } },
  { KE_KEY, 0x20e6, { KEY_PROG1 } },
  { KE_KEY, 0x20e8, { KEY_MEDIA } },
  { KE_KEY, 0x2142, { KEY_MEDIA } },
  { KE_KEY, 0x213b, { KEY_INFO } },
  { KE_KEY, 0x2169, { KEY_ROTATE_DISPLAY } },
  { KE_KEY, 0x216a, { KEY_SETUP } },
  { KE_KEY, 0x231b, { KEY_HELP } },
  //{ KE_KEY, 0x21A4, { KEY_F23 } }, // Winlock hotkey
  //{ KE_KEY, 0x21A5, { KEY_F23 } }, // Omen key
  //{ KE_KEY, 0x21A7, { KEY_F24 } }, // ???
  //{ KE_KEY, 0x21A9, { KEY_F24 } }, // Disable touchpad hotkey
  { KE_END, 0 }
};

static struct input_dev *hp_wmi_input_dev;
static struct platform_device *hp_wmi_platform_dev;

static struct rfkill *wifi_rfkill;
static struct rfkill *bluetooth_rfkill;
static struct rfkill *wwan_rfkill;

struct rfkill2_device {
  u8 id;
  int num;
  struct rfkill *rfkill;
};

static int rfkill2_count;
static struct rfkill2_device rfkill2[HPWMI_MAX_RFKILL2_DEVICES];

/* Determine featureset for specific models */

struct quirk_entry {
  bool fourzone;
};

static struct quirk_entry temp_omen = {
  .fourzone = true,
};

static struct quirk_entry *quirks = &temp_omen;

/* map output size to the corresponding WMI method id */
static inline int encode_outsize_for_pvsz(int outsize)
{
  if (outsize > 4096)
    return -EINVAL;
  if (outsize > 1024)
    return 5;
  if (outsize > 128)
    return 4;
  if (outsize > 4)
    return 3;
  if (outsize > 0)
    return 2;
  return 1;
}

/*
 * hp_wmi_perform_query
 *
 * query:	The commandtype (enum hp_wmi_commandtype)
 * write:	The command (enum hp_wmi_command)
 * buffer:	Buffer used as input and/or output
 * insize:	Size of input buffer
 * outsize:	Size of output buffer
 *
 * returns zero on success
 *         an HP WMI query specific error code (which is positive)
 *         -EINVAL if the query was not successful at all
 *         -EINVAL if the output buffer size exceeds buffersize
 *
 * Note: The buffersize must at least be the maximum of the input and output
 *       size. E.g. Battery info query is defined to have 1 byte input
 *       and 128 byte output. The caller would do:
 *       buffer = kzalloc(128, GFP_KERNEL);
 *       ret = hp_wmi_perform_query(HPWMI_BATTERY_QUERY, HPWMI_READ, buffer, 1, 128)
 */
static int hp_wmi_perform_query(int query, enum hp_wmi_command command,
        void *buffer, int insize, int outsize)
{
  int mid;
  struct bios_return *bios_return;
  int actual_outsize;
  union acpi_object *obj;
  struct bios_args args = {
    .signature = 0x55434553,
    .command = command,
    .commandtype = query,
    .datasize = insize,
    .data = { 0 },
  };
  struct acpi_buffer input = { sizeof(struct bios_args), &args };
  struct acpi_buffer output = { ACPI_ALLOCATE_BUFFER, NULL };
  int ret = 0;

  mid = encode_outsize_for_pvsz(outsize);
  if (WARN_ON(mid < 0))
    return mid;

  if (WARN_ON(insize > sizeof(args.data)))
    return -EINVAL;
  memcpy(&args.data[0], buffer, insize);

  wmi_evaluate_method(HPWMI_BIOS_GUID, 0, mid, &input, &output);

  obj = output.pointer;

  if (!obj)
    return -EINVAL;

  if (obj->type != ACPI_TYPE_BUFFER) {
    ret = -EINVAL;
    goto out_free;
  }

  bios_return = (struct bios_return *)obj->buffer.pointer;
  ret = bios_return->return_code;

  if (ret) {
    if (ret != HPWMI_RET_UNKNOWN_CMDTYPE)
      pr_warn("query 0x%x returned error 0x%x\n", query, ret);
    goto out_free;
  }

  /* Ignore output data of zero size */
  if (!outsize)
    goto out_free;

  actual_outsize = min(outsize, (int)(obj->buffer.length - sizeof(*bios_return)));
  memcpy(buffer, obj->buffer.pointer + sizeof(*bios_return), actual_outsize);
  memset(buffer + actual_outsize, 0, outsize - actual_outsize);

out_free:
  kfree(obj);
  return ret;
}

static int hp_wmi_read_int(int query)
{
  int val = 0, ret;

  ret = hp_wmi_perform_query(query, HPWMI_READ, &val,
           sizeof(val), sizeof(val));

  if (ret)
    return ret < 0 ? ret : -EINVAL;

  return val;
}

static int hp_wmi_hw_state(int mask)
{
  int state = hp_wmi_read_int(HPWMI_HARDWARE_QUERY);

  if (state < 0)
    return state;

  return !!(state & mask);
}

static int __init hp_wmi_bios_2008_later(void)
{
  int state = 0;
  int ret = hp_wmi_perform_query(HPWMI_FEATURE_QUERY, HPWMI_READ, &state,
               sizeof(state), sizeof(state));
  if (!ret)
    return 1;

  return (ret == HPWMI_RET_UNKNOWN_CMDTYPE) ? 0 : -ENXIO;
}

static int __init hp_wmi_bios_2009_later(void)
{
  u8 state[128];
  int ret = hp_wmi_perform_query(HPWMI_FEATURE2_QUERY, HPWMI_READ, &state,
               sizeof(state), sizeof(state));
  if (!ret)
    return 1;

  return (ret == HPWMI_RET_UNKNOWN_CMDTYPE) ? 0 : -ENXIO;
}

static int __init hp_wmi_enable_hotkeys(void)
{
  int value = 0x6e;
  int ret = hp_wmi_perform_query(HPWMI_BIOS_QUERY, HPWMI_WRITE, &value,
               sizeof(value), 0);

  return ret <= 0 ? ret : -EINVAL;
}

static int hp_wmi_set_block(void *data, bool blocked)
{
  enum hp_wmi_radio r = (enum hp_wmi_radio) data;
  int query = BIT(r + 8) | ((!blocked) << r);
  int ret;

  ret = hp_wmi_perform_query(HPWMI_WIRELESS_QUERY, HPWMI_WRITE,
           &query, sizeof(query), 0);

  return ret <= 0 ? ret : -EINVAL;
}

static const struct rfkill_ops hp_wmi_rfkill_ops = {
  .set_block = hp_wmi_set_block,
};

static bool hp_wmi_get_sw_state(enum hp_wmi_radio r)
{
  int mask = 0x200 << (r * 8);

  int wireless = hp_wmi_read_int(HPWMI_WIRELESS_QUERY);

  /* TBD: Pass error */
  WARN_ONCE(wireless < 0, "error executing HPWMI_WIRELESS_QUERY");

  return !(wireless & mask);
}

static bool hp_wmi_get_hw_state(enum hp_wmi_radio r)
{
  int mask = 0x800 << (r * 8);

  int wireless = hp_wmi_read_int(HPWMI_WIRELESS_QUERY);

  /* TBD: Pass error */
  WARN_ONCE(wireless < 0, "error executing HPWMI_WIRELESS_QUERY");

  return !(wireless & mask);
}

static int hp_wmi_rfkill2_set_block(void *data, bool blocked)
{
  int rfkill_id = (int)(long)data;
  char buffer[4] = { 0x01, 0x00, rfkill_id, !blocked };
  int ret;

  ret = hp_wmi_perform_query(HPWMI_WIRELESS2_QUERY, HPWMI_WRITE,
           buffer, sizeof(buffer), 0);

  return ret <= 0 ? ret : -EINVAL;
}

static const struct rfkill_ops hp_wmi_rfkill2_ops = {
  .set_block = hp_wmi_rfkill2_set_block,
};

static int hp_wmi_rfkill2_refresh(void)
{
  struct bios_rfkill2_state state;
  int err, i;

  err = hp_wmi_perform_query(HPWMI_WIRELESS2_QUERY, HPWMI_READ, &state,
           sizeof(state), sizeof(state));
  if (err)
    return err;

  for (i = 0; i < rfkill2_count; i++) {
    int num = rfkill2[i].num;
    struct bios_rfkill2_device_state *devstate;
    devstate = &state.device[num];

    if (num >= state.count ||
        devstate->rfkill_id != rfkill2[i].id) {
      pr_warn("power configuration of the wireless devices unexpectedly changed\n");
      continue;
    }

    rfkill_set_states(rfkill2[i].rfkill,
          IS_SWBLOCKED(devstate->power),
          IS_HWBLOCKED(devstate->power));
  }

  return 0;
}

static ssize_t display_show(struct device *dev, struct device_attribute *attr,
          char *buf)
{
  int value = hp_wmi_read_int(HPWMI_DISPLAY_QUERY);
  if (value < 0)
    return value;
  return sprintf(buf, "%d\n", value);
}

static ssize_t hddtemp_show(struct device *dev, struct device_attribute *attr,
          char *buf)
{
  int value = hp_wmi_read_int(HPWMI_HDDTEMP_QUERY);
  if (value < 0)
    return value;
  return sprintf(buf, "%d\n", value);
}

static ssize_t als_show(struct device *dev, struct device_attribute *attr,
      char *buf)
{
  int value = hp_wmi_read_int(HPWMI_ALS_QUERY);
  if (value < 0)
    return value;
  return sprintf(buf, "%d\n", value);
}

static ssize_t dock_show(struct device *dev, struct device_attribute *attr,
       char *buf)
{
  int value = hp_wmi_hw_state(HPWMI_DOCK_MASK);
  if (value < 0)
    return value;
  return sprintf(buf, "%d\n", value);
}

static ssize_t tablet_show(struct device *dev, struct device_attribute *attr,
         char *buf)
{
  int value = hp_wmi_hw_state(HPWMI_TABLET_MASK);
  if (value < 0)
    return value;
  return sprintf(buf, "%d\n", value);
}

static ssize_t postcode_show(struct device *dev, struct device_attribute *attr,
           char *buf)
{
  /* Get the POST error code of previous boot failure. */
  int value = hp_wmi_read_int(HPWMI_POSTCODEERROR_QUERY);
  if (value < 0)
    return value;
  return sprintf(buf, "0x%x\n", value);
}

static ssize_t als_store(struct device *dev, struct device_attribute *attr,
       const char *buf, size_t count)
{
  u32 tmp = simple_strtoul(buf, NULL, 10);
  int ret = hp_wmi_perform_query(HPWMI_ALS_QUERY, HPWMI_WRITE, &tmp,
               sizeof(tmp), sizeof(tmp));
  if (ret)
    return ret < 0 ? ret : -EINVAL;

  return count;
}

static ssize_t postcode_store(struct device *dev, struct device_attribute *attr,
            const char *buf, size_t count)
{
  long unsigned int tmp2;
  int ret;
  u32 tmp;

  ret = kstrtoul(buf, 10, &tmp2);
  if (!ret && tmp2 != 1)
    ret = -EINVAL;
  if (ret)
    goto out;

  /* Clear the POST error code. It is kept until until cleared. */
  tmp = (u32) tmp2;
  ret = hp_wmi_perform_query(HPWMI_POSTCODEERROR_QUERY, HPWMI_WRITE, &tmp,
               sizeof(tmp), sizeof(tmp));

out:
  if (ret)
    return ret < 0 ? ret : -EINVAL;

  return count;
}

static DEVICE_ATTR_RO(display);
static DEVICE_ATTR_RO(hddtemp);
static DEVICE_ATTR_RW(als);
static DEVICE_ATTR_RO(dock);
static DEVICE_ATTR_RO(tablet);
static DEVICE_ATTR_RW(postcode);

static void hp_wmi_notify(u32 value, void *context)
{
  struct acpi_buffer response = { ACPI_ALLOCATE_BUFFER, NULL };
  u32 event_id, event_data;
  union acpi_object *obj;
  acpi_status status;
  u32 *location;
  int key_code;

  status = wmi_get_event_data(value, &response);
  if (status == AE_NOT_FOUND)
  {
    // We've been woken up without any event data
    // Some models do this when the Omen hotkey is pressed
    event_id = HPWMI_OMEN_KEY;
  }
  else if (status != AE_OK) {
    pr_info("bad event value 0x%x status 0x%x\n", value, status);
    return;
  }
  else
  {
    obj = (union acpi_object *)response.pointer;

    if (!obj)
      return;
    if (obj->type != ACPI_TYPE_BUFFER) {
      pr_info("Unknown response received %d\n", obj->type);
      kfree(obj);
      return;
    }

    /*
    * Depending on ACPI version the concatenation of id and event data
    * inside _WED function will result in a 8 or 16 byte buffer.
    */
    location = (u32 *)obj->buffer.pointer;
    if (obj->buffer.length == 8) {
      event_id = *location;
      event_data = *(location + 1);
    } else if (obj->buffer.length == 16) {
      event_id = *location;
      event_data = *(location + 2);
    } else {
      pr_info("Unknown buffer length %d\n", obj->buffer.length);
      kfree(obj);
      return;
    }
    kfree(obj);
  }

  switch (event_id) {
  case HPWMI_DOCK_EVENT:
    if (test_bit(SW_DOCK, hp_wmi_input_dev->swbit))
      input_report_switch(hp_wmi_input_dev, SW_DOCK,
              hp_wmi_hw_state(HPWMI_DOCK_MASK));
    if (test_bit(SW_TABLET_MODE, hp_wmi_input_dev->swbit))
      input_report_switch(hp_wmi_input_dev, SW_TABLET_MODE,
              hp_wmi_hw_state(HPWMI_TABLET_MASK));
    input_sync(hp_wmi_input_dev);
    break;
  case HPWMI_PARK_HDD:
    break;
  case HPWMI_SMART_ADAPTER:
    break;
  case HPWMI_BEZEL_BUTTON:
  case HPWMI_OMEN_KEY:
    key_code = hp_wmi_read_int(HPWMI_HOTKEY_QUERY);
    if (key_code < 0)
      break;

    if (!sparse_keymap_report_event(hp_wmi_input_dev,
            key_code, 1, true))
      pr_info("Unknown key code - 0x%x\n", key_code);
    break;
  case HPWMI_WIRELESS:
    if (rfkill2_count) {
      hp_wmi_rfkill2_refresh();
      break;
    }

    if (wifi_rfkill)
      rfkill_set_states(wifi_rfkill,
            hp_wmi_get_sw_state(HPWMI_WIFI),
            hp_wmi_get_hw_state(HPWMI_WIFI));
    if (bluetooth_rfkill)
      rfkill_set_states(bluetooth_rfkill,
            hp_wmi_get_sw_state(HPWMI_BLUETOOTH),
            hp_wmi_get_hw_state(HPWMI_BLUETOOTH));
    if (wwan_rfkill)
      rfkill_set_states(wwan_rfkill,
            hp_wmi_get_sw_state(HPWMI_WWAN),
            hp_wmi_get_hw_state(HPWMI_WWAN));
    break;
  case HPWMI_CPU_BATTERY_THROTTLE:
    pr_info("Unimplemented CPU throttle because of 3 Cell battery event detected\n");
    break;
  case HPWMI_LOCK_SWITCH:
    break;
  case HPWMI_LID_SWITCH:
    break;
  case HPWMI_SCREEN_ROTATION:
    break;
  case HPWMI_COOLSENSE_SYSTEM_MOBILE:
    break;
  case HPWMI_COOLSENSE_SYSTEM_HOT:
    break;
  case HPWMI_PROXIMITY_SENSOR:
    break;
  case HPWMI_BACKLIT_KB_BRIGHTNESS:
    break;
  case HPWMI_PEAKSHIFT_PERIOD:
    break;
  case HPWMI_BATTERY_CHARGE_PERIOD:
    break;
  default:
    pr_info("Unknown event_id - %d - 0x%x\n", event_id, event_data);
    break;
  }
}

static int __init hp_wmi_input_setup(void)
{
  acpi_status status;
  int err, val;

  hp_wmi_input_dev = input_allocate_device();
  if (!hp_wmi_input_dev)
    return -ENOMEM;

  hp_wmi_input_dev->name = "HP WMI hotkeys";
  hp_wmi_input_dev->phys = "wmi/input0";
  hp_wmi_input_dev->id.bustype = BUS_HOST;

  __set_bit(EV_SW, hp_wmi_input_dev->evbit);

  /* Dock */
  val = hp_wmi_hw_state(HPWMI_DOCK_MASK);
  if (!(val < 0)) {
    __set_bit(SW_DOCK, hp_wmi_input_dev->swbit);
    input_report_switch(hp_wmi_input_dev, SW_DOCK, val);
  }

  /* Tablet mode */
  val = hp_wmi_hw_state(HPWMI_TABLET_MASK);
  if (!(val < 0)) {
    __set_bit(SW_TABLET_MODE, hp_wmi_input_dev->swbit);
    input_report_switch(hp_wmi_input_dev, SW_TABLET_MODE, val);
  }

  err = sparse_keymap_setup(hp_wmi_input_dev, hp_wmi_keymap, NULL);
  if (err)
    goto err_free_dev;

  /* Set initial hardware state */
  input_sync(hp_wmi_input_dev);

  if (!hp_wmi_bios_2009_later() && hp_wmi_bios_2008_later())
    hp_wmi_enable_hotkeys();

  status = wmi_install_notify_handler(HPWMI_EVENT_GUID, hp_wmi_notify, NULL);
  if (ACPI_FAILURE(status)) {
    err = -EIO;
    goto err_free_dev;
  }

  err = input_register_device(hp_wmi_input_dev);
  if (err)
    goto err_uninstall_notifier;

  return 0;

 err_uninstall_notifier:
  wmi_remove_notify_handler(HPWMI_EVENT_GUID);
 err_free_dev:
  input_free_device(hp_wmi_input_dev);
  return err;
}

static void hp_wmi_input_destroy(void)
{
  wmi_remove_notify_handler(HPWMI_EVENT_GUID);
  input_unregister_device(hp_wmi_input_dev);
}

static void cleanup_sysfs(struct platform_device *device)
{
  device_remove_file(&device->dev, &dev_attr_display);
  device_remove_file(&device->dev, &dev_attr_hddtemp);
  device_remove_file(&device->dev, &dev_attr_als);
  device_remove_file(&device->dev, &dev_attr_dock);
  device_remove_file(&device->dev, &dev_attr_tablet);
  device_remove_file(&device->dev, &dev_attr_postcode);
}

static int __init hp_wmi_rfkill_setup(struct platform_device *device)
{
  int err, wireless;

  wireless = hp_wmi_read_int(HPWMI_WIRELESS_QUERY);
  if (wireless < 0)
    return wireless;

  err = hp_wmi_perform_query(HPWMI_WIRELESS_QUERY, HPWMI_WRITE, &wireless,
           sizeof(wireless), 0);
  if (err)
    return err;

  if (wireless & 0x1) {
    wifi_rfkill = rfkill_alloc("hp-wifi", &device->dev,
             RFKILL_TYPE_WLAN,
             &hp_wmi_rfkill_ops,
             (void *) HPWMI_WIFI);
    if (!wifi_rfkill)
      return -ENOMEM;
    rfkill_init_sw_state(wifi_rfkill,
             hp_wmi_get_sw_state(HPWMI_WIFI));
    rfkill_set_hw_state(wifi_rfkill,
            hp_wmi_get_hw_state(HPWMI_WIFI));
    err = rfkill_register(wifi_rfkill);
    if (err)
      goto register_wifi_error;
  }

  if (wireless & 0x2) {
    bluetooth_rfkill = rfkill_alloc("hp-bluetooth", &device->dev,
            RFKILL_TYPE_BLUETOOTH,
            &hp_wmi_rfkill_ops,
            (void *) HPWMI_BLUETOOTH);
    if (!bluetooth_rfkill) {
      err = -ENOMEM;
      goto register_bluetooth_error;
    }
    rfkill_init_sw_state(bluetooth_rfkill,
             hp_wmi_get_sw_state(HPWMI_BLUETOOTH));
    rfkill_set_hw_state(bluetooth_rfkill,
            hp_wmi_get_hw_state(HPWMI_BLUETOOTH));
    err = rfkill_register(bluetooth_rfkill);
    if (err)
      goto register_bluetooth_error;
  }

  if (wireless & 0x4) {
    wwan_rfkill = rfkill_alloc("hp-wwan", &device->dev,
             RFKILL_TYPE_WWAN,
             &hp_wmi_rfkill_ops,
             (void *) HPWMI_WWAN);
    if (!wwan_rfkill) {
      err = -ENOMEM;
      goto register_wwan_error;
    }
    rfkill_init_sw_state(wwan_rfkill,
             hp_wmi_get_sw_state(HPWMI_WWAN));
    rfkill_set_hw_state(wwan_rfkill,
            hp_wmi_get_hw_state(HPWMI_WWAN));
    err = rfkill_register(wwan_rfkill);
    if (err)
      goto register_wwan_error;
  }

  return 0;

register_wwan_error:
  rfkill_destroy(wwan_rfkill);
  wwan_rfkill = NULL;
  if (bluetooth_rfkill)
    rfkill_unregister(bluetooth_rfkill);
register_bluetooth_error:
  rfkill_destroy(bluetooth_rfkill);
  bluetooth_rfkill = NULL;
  if (wifi_rfkill)
    rfkill_unregister(wifi_rfkill);
register_wifi_error:
  rfkill_destroy(wifi_rfkill);
  wifi_rfkill = NULL;
  return err;
}

static int __init hp_wmi_rfkill2_setup(struct platform_device *device)
{
  struct bios_rfkill2_state state;
  int err, i;

  err = hp_wmi_perform_query(HPWMI_WIRELESS2_QUERY, HPWMI_READ, &state,
           sizeof(state), sizeof(state));
  if (err)
    return err < 0 ? err : -EINVAL;

  if (state.count > HPWMI_MAX_RFKILL2_DEVICES) {
    pr_warn("unable to parse 0x1b query output\n");
    return -EINVAL;
  }

  for (i = 0; i < state.count; i++) {
    struct rfkill *rfkill;
    enum rfkill_type type;
    char *name;
    switch (state.device[i].radio_type) {
    case HPWMI_WIFI:
      type = RFKILL_TYPE_WLAN;
      name = "hp-wifi";
      break;
    case HPWMI_BLUETOOTH:
      type = RFKILL_TYPE_BLUETOOTH;
      name = "hp-bluetooth";
      break;
    case HPWMI_WWAN:
      type = RFKILL_TYPE_WWAN;
      name = "hp-wwan";
      break;
    case HPWMI_GPS:
      type = RFKILL_TYPE_GPS;
      name = "hp-gps";
      break;
    default:
      pr_warn("unknown device type 0x%x\n",
        state.device[i].radio_type);
      continue;
    }

    if (!state.device[i].vendor_id) {
      pr_warn("zero device %d while %d reported\n",
        i, state.count);
      continue;
    }

    rfkill = rfkill_alloc(name, &device->dev, type,
              &hp_wmi_rfkill2_ops, (void *)(long)i);
    if (!rfkill) {
      err = -ENOMEM;
      goto fail;
    }

    rfkill2[rfkill2_count].id = state.device[i].rfkill_id;
    rfkill2[rfkill2_count].num = i;
    rfkill2[rfkill2_count].rfkill = rfkill;

    rfkill_init_sw_state(rfkill, IS_SWBLOCKED(state.device[i].power));
    rfkill_set_hw_state(rfkill, IS_HWBLOCKED(state.device[i].power));

    if (!(state.device[i].power & HPWMI_POWER_BIOS))
      pr_info("device %s blocked by BIOS\n", name);

    err = rfkill_register(rfkill);
    if (err) {
      rfkill_destroy(rfkill);
      goto fail;
    }

    rfkill2_count++;
  }

  return 0;
fail:
  for (; rfkill2_count > 0; rfkill2_count--) {
    rfkill_unregister(rfkill2[rfkill2_count - 1].rfkill);
    rfkill_destroy(rfkill2[rfkill2_count - 1].rfkill);
  }
  return err;
}

/* Support for the HP Omen FourZone keyboard lighting */

#define FOURZONE_COUNT 4

struct color_platform {
  u8 blue;
  u8 green;
  u8 red;
} __packed;

struct platform_zone {
  u8 offset;
  struct device_attribute *attr;
  struct color_platform colors;
};

static struct device_attribute *zone_dev_attrs;
static struct attribute **zone_attrs;
static struct platform_zone *zone_data;

static struct attribute_group zone_attribute_group = {
  .name = "rgb_zones",
};

/*
 * Helpers used for zone control
 */
static int parse_rgb(const char *buf, struct platform_zone *zone)
{
  long unsigned int rgb;
  int ret;
  union color_union {
    struct color_platform cp;
    int package;
  } repackager;

  ret = kstrtoul(buf, 16, &rgb);
  if (ret)
    return ret;

  /* RGB triplet notation is 24-bit hexadecimal */
  if (rgb > 0xFFFFFF)
    return -EINVAL;

  repackager.package = rgb;
  pr_debug("hp-wmi: r:%d g:%d b:%d\n",
     repackager.cp.red, repackager.cp.green, repackager.cp.blue);
  zone->colors = repackager.cp;
  return 0;
}

static struct platform_zone *match_zone(struct device_attribute *attr)
{
  u8 zone;

  for (zone = 0; zone < FOURZONE_COUNT; zone++) {
    if ((struct device_attribute *)zone_data[zone].attr == attr) {
      pr_debug("hp-wmi: matched zone location: %d\n",
         zone_data[zone].offset);
      return &zone_data[zone];
    }
  }
  return NULL;
}

/*
 * Individual RGB zone control
 */
static int fourzone_update_led(struct platform_zone *zone, enum hp_wmi_command read_or_write)
{
  u8 state[128];

  int ret = hp_wmi_perform_query(HPWMI_FOURZONE_COLOR_GET, HPWMI_FOURZONE, &state,
    sizeof(state), sizeof(state));

  if (ret) {
    pr_warn("fourzone_color_get returned error 0x%x\n", ret);
    return ret <= 0 ? ret : -EINVAL;
  }

  if (read_or_write == HPWMI_WRITE) {
    // Zones start at offset 25. Wonder what's in the rest of the buffer?
    state[zone->offset + 0] = zone->colors.red;
    state[zone->offset + 1] = zone->colors.green;
    state[zone->offset + 2] = zone->colors.blue;

    ret = hp_wmi_perform_query(HPWMI_FOURZONE_COLOR_SET, HPWMI_FOURZONE, &state,
            sizeof(state), sizeof(state));

    if (ret)
      pr_warn("fourzone_color_set returned error 0x%x\n", ret);
    return ret;

  } else {
      zone->colors.red = state[zone->offset + 0];
      zone->colors.green = state[zone->offset + 1];
      zone->colors.blue = state[zone->offset + 2];
  }
  return 0;
}

static ssize_t zone_show(struct device *dev, struct device_attribute *attr,
       char *buf)
{
  struct platform_zone *target_zone;
  int ret;

  target_zone = match_zone(attr);
  if (target_zone == NULL)
    return sprintf(buf, "red: -1, green: -1, blue: -1\n");

  ret = fourzone_update_led(target_zone, HPWMI_READ);

  if (ret)
    return sprintf(buf, "red: -1, green: -1, blue: -1\n");

  return sprintf(buf, "red: %d, green: %d, blue: %d\n",
           target_zone->colors.red,
           target_zone->colors.green, target_zone->colors.blue);

}

static ssize_t zone_set(struct device *dev, struct device_attribute *attr,
      const char *buf, size_t count)
{
  struct platform_zone *target_zone;
  int ret;
  target_zone = match_zone(attr);
  if (target_zone == NULL) {
    pr_err("hp-wmi: invalid target zone\n");
    return 1;
  }
  ret = parse_rgb(buf, target_zone);
  if (ret)
    return ret;
  ret = fourzone_update_led(target_zone, HPWMI_WRITE);
  return ret ? ret : count;
}

/*
static void global_led_set(struct led_classdev *led_cdev,
         enum led_brightness brightness)
{
  int ret;
  global_brightness = brightness;
  ret = alienware_update_led(&zone_data[0]);
  if (ret)
    pr_err("LED brightness update failed\n");
}

static enum led_brightness global_led_get(struct led_classdev *led_cdev)
{
  return global_brightness;
}

static struct led_classdev global_led = {
  .brightness_set = global_led_set,
  .brightness_get = global_led_get,
  .name = "hp-omen::global_brightness",
};
*/

// static DEVICE_ATTR(lighting_control_state, 0644, show_control_state,
// 		   store_control_state);

static int fourzone_setup(struct platform_device *dev)
{
  u8 zone;
  char buffer[10];
  char *name;

  if (!quirks->fourzone)
    return 0;

  // global_led.max_brightness = 0x0F;
  // global_brightness = global_led.max_brightness;

  /*
   *      - zone_dev_attrs num_zones + 1 is for individual zones and then
   *        null terminated
   *      - zone_attrs num_zones + 2 is for all attrs in zone_dev_attrs +
   *        the lighting control + null terminated
   *      - zone_data num_zones is for the distinct zones
   */

  zone_dev_attrs =
      kcalloc(FOURZONE_COUNT + 1, sizeof(struct device_attribute),
        GFP_KERNEL);
  if (!zone_dev_attrs)
    return -ENOMEM;

  zone_attrs =
      kcalloc(FOURZONE_COUNT + 1 /* 2 */, sizeof(struct attribute *),
        GFP_KERNEL);
  if (!zone_attrs)
    return -ENOMEM;

  zone_data =
      kcalloc(FOURZONE_COUNT, sizeof(struct platform_zone),
        GFP_KERNEL);
  if (!zone_data)
    return -ENOMEM;

  for (zone = 0; zone < FOURZONE_COUNT; zone++) {
    sprintf(buffer, "zone%02hhX", zone);
    name = kstrdup(buffer, GFP_KERNEL);
    if (name == NULL)
      return 1;
    sysfs_attr_init(&zone_dev_attrs[zone].attr);
    zone_dev_attrs[zone].attr.name = name;
    zone_dev_attrs[zone].attr.mode = 0644;
    zone_dev_attrs[zone].show = zone_show;
    zone_dev_attrs[zone].store = zone_set;
    zone_data[zone].offset = 25 + (zone * 3);
    zone_attrs[zone] = &zone_dev_attrs[zone].attr;
    zone_data[zone].attr = &zone_dev_attrs[zone];
  }
  // zone_attrs[FOURZONE_COUNT] = &dev_attr_lighting_control_state.attr;
  zone_attribute_group.attrs = zone_attrs;

//  led_classdev_register(&dev->dev, &global_led);

  return sysfs_create_group(&dev->dev.kobj, &zone_attribute_group);
}

static int __init hp_wmi_bios_setup(struct platform_device *device)
{
  int err;

  /* clear detected rfkill devices */
  wifi_rfkill = NULL;
  bluetooth_rfkill = NULL;
  wwan_rfkill = NULL;
  rfkill2_count = 0;

  if (hp_wmi_rfkill_setup(device))
    hp_wmi_rfkill2_setup(device);

  fourzone_setup(device);

  err = device_create_file(&device->dev, &dev_attr_display);
  if (err)
    goto add_sysfs_error;
  err = device_create_file(&device->dev, &dev_attr_hddtemp);
  if (err)
    goto add_sysfs_error;
  err = device_create_file(&device->dev, &dev_attr_als);
  if (err)
    goto add_sysfs_error;
  err = device_create_file(&device->dev, &dev_attr_dock);
  if (err)
    goto add_sysfs_error;
  err = device_create_file(&device->dev, &dev_attr_tablet);
  if (err)
    goto add_sysfs_error;
  err = device_create_file(&device->dev, &dev_attr_postcode);
  if (err)
    goto add_sysfs_error;

  return 0;

add_sysfs_error:
  cleanup_sysfs(device);
  return err;
}

static int __exit hp_wmi_bios_remove(struct platform_device *device)
{
  int i;
  cleanup_sysfs(device);

  for (i = 0; i < rfkill2_count; i++) {
    rfkill_unregister(rfkill2[i].rfkill);
    rfkill_destroy(rfkill2[i].rfkill);
  }

  if (wifi_rfkill) {
    rfkill_unregister(wifi_rfkill);
    rfkill_destroy(wifi_rfkill);
  }
  if (bluetooth_rfkill) {
    rfkill_unregister(bluetooth_rfkill);
    rfkill_destroy(bluetooth_rfkill);
  }
  if (wwan_rfkill) {
    rfkill_unregister(wwan_rfkill);
    rfkill_destroy(wwan_rfkill);
  }

  return 0;
}

static int hp_wmi_resume_handler(struct device *device)
{
  /*
   * Hardware state may have changed while suspended, so trigger
   * input events for the current state. As this is a switch,
   * the input layer will only actually pass it on if the state
   * changed.
   */
  if (hp_wmi_input_dev) {
    if (test_bit(SW_DOCK, hp_wmi_input_dev->swbit))
      input_report_switch(hp_wmi_input_dev, SW_DOCK,
              hp_wmi_hw_state(HPWMI_DOCK_MASK));
    if (test_bit(SW_TABLET_MODE, hp_wmi_input_dev->swbit))
      input_report_switch(hp_wmi_input_dev, SW_TABLET_MODE,
              hp_wmi_hw_state(HPWMI_TABLET_MASK));
    input_sync(hp_wmi_input_dev);
  }

  if (rfkill2_count)
    hp_wmi_rfkill2_refresh();

  if (wifi_rfkill)
    rfkill_set_states(wifi_rfkill,
          hp_wmi_get_sw_state(HPWMI_WIFI),
          hp_wmi_get_hw_state(HPWMI_WIFI));
  if (bluetooth_rfkill)
    rfkill_set_states(bluetooth_rfkill,
          hp_wmi_get_sw_state(HPWMI_BLUETOOTH),
          hp_wmi_get_hw_state(HPWMI_BLUETOOTH));
  if (wwan_rfkill)
    rfkill_set_states(wwan_rfkill,
          hp_wmi_get_sw_state(HPWMI_WWAN),
          hp_wmi_get_hw_state(HPWMI_WWAN));

  return 0;
}

static const struct dev_pm_ops hp_wmi_pm_ops = {
  .resume  = hp_wmi_resume_handler,
  .restore  = hp_wmi_resume_handler,
};

static struct platform_driver hp_wmi_driver = {
  .driver = {
    .name = "hp-wmi",
    .pm = &hp_wmi_pm_ops,
  },
  .remove = __exit_p(hp_wmi_bios_remove),
};

static int __init hp_wmi_init(void)
{
  int event_capable = wmi_has_guid(HPWMI_EVENT_GUID);
  int bios_capable = wmi_has_guid(HPWMI_BIOS_GUID);
  int err;

  if (!bios_capable && !event_capable)
    return -ENODEV;

  if (event_capable) {
    err = hp_wmi_input_setup();
    if (err)
      return err;
  }

  if (bios_capable) {
    hp_wmi_platform_dev =
      platform_device_register_simple("hp-wmi", -1, NULL, 0);
    if (IS_ERR(hp_wmi_platform_dev)) {
      err = PTR_ERR(hp_wmi_platform_dev);
      goto err_destroy_input;
    }

    err = platform_driver_probe(&hp_wmi_driver, hp_wmi_bios_setup);
    if (err)
      goto err_unregister_device;
  }

  return 0;

err_unregister_device:
  platform_device_unregister(hp_wmi_platform_dev);
err_destroy_input:
  if (event_capable)
    hp_wmi_input_destroy();

  return err;
}
module_init(hp_wmi_init);

static void __exit hp_wmi_exit(void)
{
  if (wmi_has_guid(HPWMI_EVENT_GUID))
    hp_wmi_input_destroy();

  if (hp_wmi_platform_dev) {
    platform_device_unregister(hp_wmi_platform_dev);
    platform_driver_unregister(&hp_wmi_driver);
  }
}
module_exit(hp_wmi_exit);
