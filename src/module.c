#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>

//#include <zmk/events/battery_state_changed.h>
#include <zmk/event_manager.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/usb.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

BUILD_ASSERT(DT_NODE_EXISTS(DT_NODELABEL(red_led)), "Node 'red_led' not found.");
BUILD_ASSERT(DT_NODE_EXISTS(DT_NODELABEL(green_led)), "Node 'green_led' not found.");

BUILD_ASSERT(DT_NODE_HAS_PROP(DT_NODELABEL(charge_status), stat1_gpios));
BUILD_ASSERT(DT_NODE_HAS_PROP(DT_NODELABEL(charge_status), stat2_gpios));

static const struct gpio_dt_spec led_red = GPIO_DT_SPEC_GET(DT_NODELABEL(red_led), gpios);
static const struct gpio_dt_spec led_green = GPIO_DT_SPEC_GET(DT_NODELABEL(green_led), gpios);

static const struct gpio_dt_spec stat1_pin = GPIO_DT_SPEC_GET(DT_NODELABEL(charge_status), stat1_gpios);
static const struct gpio_dt_spec stat2_pin = GPIO_DT_SPEC_GET(DT_NODELABEL(charge_status), stat2_gpios);

static struct k_work_delayable init_bat_work;
static struct k_work_delayable stat_bat_work;
static struct gpio_callback stat1_cb_data;
static struct gpio_callback stat2_cb_data;

static bool stat1_enabled = false;
static bool stat2_enabled = false;

static void update_charge_status(void){
	bool usb_power = zmk_usb_is_powered();

	bool is_charging = (stat1_enabled && !stat2_enabled) && usb_power;
	bool finished_charging = (stat1_enabled && stat2_enabled) && usb_power;

	if(is_charging){
		int r1 = gpio_pin_set_dt(&led_red, 0);
		int r2 = gpio_pin_set_dt(&led_green, 1);

		if(r1 < 0 || r2 < 0){
			LOG_INF("is_charging returned %d, %d", r1, r2);
			return;
		}
	}else if(finished_charging){
		int r1 = gpio_pin_set_dt(&led_red, 1);
		int r2 = gpio_pin_set_dt(&led_green, 1);

		if(r1 < 0 || r2 < 0){
			LOG_INF("finished_charging returned %d, %d", r1, r2);
			return;
		}
	}else{
		int r1 = gpio_pin_set_dt(&led_green, 0);
		int r2 = gpio_pin_set_dt(&led_red, 1);

		if(r1 < 0 || r2 < 0){
			LOG_INF("other returned %d, %d", r1, r2);
			return;
		}
	}
}

static int cb_usb(const zmk_event_t *eh){
	update_charge_status();
	return ZMK_EV_EVENT_HANDLED;
}

static void cb_stat_pins(const struct device *dev, struct gpio_callback *cb, uint32_t pins){
	k_work_reschedule(&cb_stat_bat_work, K_MSEC(10));
}

static void cb_stat_bat_work(struct k_work *work){
	LOG_INF("stat callback triggered");

	int s1 = gpio_pin_get_dt(&stat1_pin);
	if(s1 < 0){ LOG_INF("[stat_bat_work] Reading stat1_pin returned %d", s1); return; }

	int s2 = gpio_pin_get_dt(&stat2_pin);
	if(s2 < 0){ LOG_INF("[stat_bat_work] Reading stat1_pin returned %d", s2); return; }

	stat1_enabled = s1;
	stat2_enabled = s2;

	update_charge_status();
}

static void cb_init_bat_work(struct k_work *work){
	LOG_INF("\n");
	//@TODO: zekronz,charge-status
	//@TODO: Handle sleep

	if(!device_is_ready(led_red.port)){ LOG_INF("NOT READY: led_red"); return; }
    if(!device_is_ready(led_green.port)){ LOG_INF("NOT READY: led_green"); return; }
	if(!device_is_ready(stat1_pin.port)){ LOG_INF("NOT READY: stat1_pin"); return; }
	if(!device_is_ready(stat2_pin.port)){ LOG_INF("NOT READY: stat2_pin"); return; };

	int ret;

    ret = gpio_pin_configure_dt(&led_red, GPIO_OUTPUT_INACTIVE);
	if(ret < 0){ LOG_INF("Configuring led_red returned %d", ret); return; }

    ret = gpio_pin_configure_dt(&led_green, GPIO_OUTPUT_INACTIVE);
	if(ret < 0){ LOG_INF("Configuring led_green returned %d", ret); return; }

	ret = gpio_pin_configure_dt(&stat1_pin, GPIO_INPUT);
	if(ret < 0){ LOG_INF("Configuring stat1_pin returned %d", ret); return; }

	ret = gpio_pin_configure_dt(&stat2_pin, GPIO_INPUT);
	if(ret < 0){ LOG_INF("Configuring stat2_pin returned %d", ret); return; }

	int s1 = gpio_pin_get_dt(&stat1_pin);
	if(s1 < 0){ LOG_INF("[INIT] Reading stat1_pin returned %d", s1); return; }

	int s2 = gpio_pin_get_dt(&stat2_pin);
	if(s2 < 0){ LOG_INF("[INIT] Reading stat2_pin returned %d", s2); return; }

	stat1_enabled = s1;
	stat2_enabled = s2;

	ret = gpio_pin_interrupt_configure_dt(&stat1_pin, GPIO_INT_EDGE_BOTH);
	if(ret < 0){ LOG_INF("Configuring interrupt for stat1_pin returned %d", ret); return; }

	ret = gpio_pin_interrupt_configure_dt(&stat2_pin, GPIO_INT_EDGE_BOTH);
	if(ret < 0){ LOG_INF("Configuring interrupt for stat2_pin returned %d", ret); return; }

	if(stat1_pin.port == stat2_pin.port){
		gpio_init_callback(&stat1_cb_data, cb_stat_pins, BIT(stat1_pin.pin) | BIT(stat2_pin.pin));

		ret = gpio_add_callback(stat1_pin.port, &stat1_cb_data);
		if(ret < 0){ LOG_INF("(0) Adding callback for stat1 returned %d", ret); return; }
	}else {
		gpio_init_callback(&stat1_cb_data, cb_stat_pins, BIT(stat1_pin.pin));
		gpio_init_callback(&stat2_cb_data, cb_stat_pins, BIT(stat2_pin.pin));

		ret = gpio_add_callback(stat1_pin.port, &stat1_cb_data);
		if(ret < 0){ LOG_INF("(1) Adding callback for stat1 returned %d", ret); return; }

		ret = gpio_add_callback(stat2_pin.port, &stat2_cb_data);
		if(ret < 0){ LOG_INF("(1) Adding callback for stat2 returned %d", ret); return; }
	}

	update_charge_status();

	LOG_INF("Initialized battery led indicator.");
}

static int bat_led_init(void){
	k_work_init_delayable(&init_bat_work, init_bat);
	k_work_init_delayable(&stat_bat_work, stat_bat);

	int ret;
	ret = k_work_schedule(&cb_init_bat_work, K_MSEC(200));
	if(ret < 0){ LOG_INF("init_bar_work schedule returned %d", ret); return; }

    return 0;
}

ZMK_LISTENER(usb_state_listener, cb_usb);
ZMK_SUBSCRIPTION(usb_state_listener, zmk_usb_conn_state_changed);

SYS_INIT(bat_led_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);