#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/usb/usb_device.h>
//#include <zmk/events/battery_state_changed.h>

BUILD_ASSERT(DT_NODE_EXISTS(DT_NODELABEL(red_led)), "Node 'red_led' not found.");
BUILD_ASSERT(DT_NODE_EXISTS(DT_NODELABEL(green_led)), "Node 'green_led' not found.");

BUILD_ASSERT(DT_NODE_HAS_PROP(DT_NODELABEL(charge_status), stat1_gpios));
BUILD_ASSERT(DT_NODE_HAS_PROP(DT_NODELABEL(charge_status), stat2_gpios));

static const struct gpio_dt_spec led_red = GPIO_DT_SPEC_GET(DT_NODELABEL(red_led), gpios);
static const struct gpio_dt_spec led_green = GPIO_DT_SPEC_GET(DT_NODELABEL(green_led), gpios);

static const struct gpio_dt_spec stat1_pin = GPIO_DT_SPEC_GET(DT_NODELABEL(charge_status), stat1_gpios);
static const struct gpio_dt_spec stat2_pin = GPIO_DT_SPEC_GET(DT_NODELABEL(charge_status), stat2_gpios);

static struct k_work_delayable led_work;
static struct gpio_callback stat1_cb_data;
static struct gpio_callback stat2_cb_data;

static bool stat1_enabled = false;
static bool stat2_enabled = false;
static bool usb_connected = false;

static void update_charge_status(void){
	bool is_charging = (stat1_enabled && !stat2_enabled) && usb_connected;
	bool finished_charging = (stat1_enabled && stat2_enabled) && usb_connected;

	if(is_charging){
		gpio_pin_set_dt(&led_red, 0);
		gpio_pin_set_dt(&led_green, 1);
	}else if(finished_charging){
		gpio_pin_set_dt(&led_red, 1);
		gpio_pin_set_dt(&led_green, 1);
	}else{
		gpio_pin_set_dt(&led_red, 1);
		gpio_pin_set_dt(&led_green, 0);
	}
}

static void usb_status_cb(enum usb_dc_status_code status, const uint8_t *param){
    switch(status){
    	case USB_DC_CONNECTED:
        	usb_connected = true;
			update_charge_status();
        break;

    	case USB_DC_DISCONNECTED:
        	usb_connected = false;
			update_charge_status();
        break;
    }
}

static void bat_led_work_handler(struct k_work *work){
	int s1 = gpio_pin_get_dt(&stat1_pin);
	int s2 = gpio_pin_get_dt(&stat2_pin);

	if(s1 < 0 || s2 < 0) return;

	stat1_enabled = s1;
	stat2_enabled = s2;

	update_charge_status();

	//k_work_reschedule(&led_work, K_SECONDS(1));
	
	//@TODO: Handle sleep
}

static void stat_cb(const struct device *dev, struct gpio_callback *cb, uint32_t pins){
	k_work_schedule(&led_work, K_MSEC(20));
}

static int bat_led_init(void){
    if(!device_is_ready(led_red.port)) return -ENODEV;
    if(!device_is_ready(led_green.port)) return -ENODEV;
	if(!device_is_ready(stat1_pin.port)) return -ENODEV;
	if(!device_is_ready(stat2_pin.port)) return -ENODEV;

	k_work_init_delayable(&led_work, bat_led_work_handler);

	int ret;

    ret = gpio_pin_configure_dt(&led_red, GPIO_OUTPUT_INACTIVE);
	if(ret < 0) return ret;

    ret = gpio_pin_configure_dt(&led_green, GPIO_OUTPUT_INACTIVE);
	if(ret < 0) return ret;

	ret = gpio_pin_configure_dt(&stat1_pin, GPIO_INPUT);
	if(ret < 0) return ret;

	ret = gpio_pin_configure_dt(&stat2_pin, GPIO_INPUT);
	if(ret < 0) return ret;

	int s1 = gpio_pin_get_dt(&stat1_pin);
	if(s1 < 0) return s1;

	int s2 = gpio_pin_get_dt(&stat2_pin);
	if(s2 < 0) return s2;

	stat1_enabled = s1;
	stat2_enabled = s2;

	ret = gpio_pin_interrupt_configure_dt(&stat1_pin, GPIO_INT_EDGE_BOTH);
	if(ret < 0) return ret;

	ret = gpio_pin_interrupt_configure_dt(&stat2_pin, GPIO_INT_EDGE_BOTH);
	if(ret < 0) return ret;

	if(stat1_pin.port == stat2_pin.port){
		gpio_init_callback(&stat1_cb_data, stat_cb, BIT(stat1_pin.pin) | BIT(stat2_pin.pin));

		ret = gpio_add_callback(stat1_pin.port, &stat1_cb_data);
		if(ret < 0) return ret;
	}else {
		gpio_init_callback(&stat1_cb_data, stat_cb, BIT(stat1_pin.pin));
		gpio_init_callback(&stat2_cb_data, stat_cb, BIT(stat2_pin.pin));

		ret = gpio_add_callback(stat1_pin.port, &stat1_cb_data);
		if(ret < 0) return ret;

		ret = gpio_add_callback(stat2_pin.port, &stat2_cb_data);
		if(ret < 0) return ret;
	}

	usb_dc_register_status_callback(usb_status_cb);
	usb_connected = usb_dc_is_connected();

	update_charge_status();

    return 0;
}

SYS_INIT(bat_led_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);