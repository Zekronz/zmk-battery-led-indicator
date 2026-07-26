#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>

#include <zmk/event_manager.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/events/activity_state_changed.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/battery.h>
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
static bool is_active = true;
static uint8_t bat_level = 100;

static void update_leds(void){
	if(!is_active){
		gpio_pin_set_dt(&led_red, 0);
		gpio_pin_set_dt(&led_green, 0);
		return;
	}

	bool usb_power = zmk_usb_is_powered();

	bool is_charging = (stat1_enabled && !stat2_enabled) && usb_power;
	bool finished_charging = (stat1_enabled && stat2_enabled) && usb_power;

	if(finished_charging){
		gpio_pin_set_dt(&led_red, 0);
		gpio_pin_set_dt(&led_green, 1);
		return;
	}

	if(is_charging){
		gpio_pin_set_dt(&led_red, 1); //@TODO: PWM
		gpio_pin_set_dt(&led_green, 1);
		return;
	}

	if(bat_level <= 10){
		gpio_pin_set_dt(&led_red, 1);
		gpio_pin_set_dt(&led_green, 0);
		return;
	}

	gpio_pin_set_dt(&led_red, 0);
	gpio_pin_set_dt(&led_green, 0);
}

static int cb_usb(const zmk_event_t *eh){
	update_leds();
	return ZMK_EV_EVENT_BUBBLE;
}

static int cb_activity(const zmk_event_t *eh){
	struct zmk_activity_state_changed *a = as_zmk_activity_state_changed(eh);

	bool active = (a->state == ZMK_ACTIVITY_ACTIVE);
	if(active == is_active) return ZMK_EV_EVENT_BUBBLE;

	is_active = active;

	if(is_active){
		int s1 = gpio_pin_get_dt(&stat1_pin);
		if(s1 < 0){ LOG_INF("[cb_activity] Reading stat1_pin returned %d", s1); return; }

		int s2 = gpio_pin_get_dt(&stat2_pin);
		if(s2 < 0){ LOG_INF("[cb_activity] Reading stat2_pin returned %d", s2); return; }

		stat1_enabled = s1;
		stat2_enabled = s2;

		bat_level = zmk_battery_state_of_charge();
	}

	update_leds();

	return ZMK_EV_EVENT_BUBBLE;
}

static int cb_bat(const zmk_event_t *eh){
	struct zmk_battery_state_changed *b = as_zmk_battery_state_changed(eh);
	bat_level = b->state_of_charge;
	update_leds();
	return ZMK_EV_EVENT_BUBBLE;
}

static void cb_stat_bat_work(struct k_work *work){
	LOG_INF("stat callback triggered");

	int s1 = gpio_pin_get_dt(&stat1_pin);
	if(s1 < 0){ LOG_INF("[stat_bat_work] Reading stat1_pin returned %d", s1); return; }

	int s2 = gpio_pin_get_dt(&stat2_pin);
	if(s2 < 0){ LOG_INF("[stat_bat_work] Reading stat1_pin returned %d", s2); return; }

	stat1_enabled = s1;
	stat2_enabled = s2;

	update_leds();
}

static void cb_stat_pins(const struct device *dev, struct gpio_callback *cb, uint32_t pins){
	int ret = k_work_reschedule(&stat_bat_work, K_MSEC(10));
	if(ret < 0){ LOG_INF("[cb_stat_pins] cb_stat_bat_work reschedule returned %d", ret); return; }
}

static void cb_init_bat_work(struct k_work *work){
	LOG_INF("\n");
	//@TODO: Handle sleep
	
	/*
		LED states:
			-Charging.
			-Finished charging.
			-Low battery.
			-Error state?

		Update LED state:
			-Stat pins change.
			-Usb connect/disconnect.
			-Battery state.
			-Activity state changes.

	*/

	if(!device_is_ready(led_red.port)){ LOG_INF("NOT READY: led_red"); return; }
    if(!device_is_ready(led_green.port)){ LOG_INF("NOT READY: led_green"); return; }
	if(!device_is_ready(stat1_pin.port)){ LOG_INF("NOT READY: stat1_pin"); return; }
	if(!device_is_ready(stat2_pin.port)){ LOG_INF("NOT READY: stat2_pin"); return; }

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

	bat_level = zmk_battery_state_of_charge();

	update_leds();

	LOG_INF("Initialized battery led indicator.");
}

static int bat_led_init(void){
	k_work_init_delayable(&init_bat_work, cb_init_bat_work);
	k_work_init_delayable(&stat_bat_work, cb_stat_bat_work);

	int ret;
	ret = k_work_schedule(&init_bat_work, K_MSEC(200));
	if(ret < 0){ LOG_INF("cb_init_bar_work schedule returned %d", ret); return; }

    return 0;
}

ZMK_LISTENER(usb_state_listener, cb_usb);
ZMK_SUBSCRIPTION(usb_state_listener, zmk_usb_conn_state_changed);

ZMK_LISTENER(activity_state_listener, cb_activity);
ZMK_SUBSCRIPTION(activity_state_listener, zmk_activity_state_changed);

ZMK_LISTENER(battery_state_listener, cb_bat);
ZMK_SUBSCRIPTION(battery_state_listener, zmk_battery_state_changed);

SYS_INIT(bat_led_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);