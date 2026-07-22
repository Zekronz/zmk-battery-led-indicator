#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
//#include <zephyr/drivers/led.h>
#include <zmk/battery.h>

BUILD_ASSERT(DT_NODE_EXISTS(DT_ALIAS(red_led)), "Alias 'red_led' not found.");
BUILD_ASSERT(DT_NODE_EXISTS(DT_ALIAS(green_led)), "Alias 'green_led' not found.");

static const struct gpio_dt_spec led_red = GPIO_DT_SPEC_GET(DT_ALIAS(red_led), gpios);
static const struct gpio_dt_spec led_green = GPIO_DT_SPEC_GET(DT_ALIAS(green_led), gpios);

static struct k_work_delayable led_work;
static bool led_state = false;

static void bat_led_work_handler(struct k_work *work){
    /*int battery_level = zmk_battery_state_of_charge();

    if (battery_level < 30) {
        // Low battery
        gpio_pin_set_dt(&led_red, 1);
        gpio_pin_set_dt(&led_green, 0);
    } else {
        // Battery OK
        gpio_pin_set_dt(&led_red, 0);
        gpio_pin_set_dt(&led_green, 1);
    }*/

	if(!led_state){
		gpio_pin_set_dt(&led_red, 1);
        gpio_pin_set_dt(&led_green, 0);
	}else{
		gpio_pin_set_dt(&led_red, 0);
        gpio_pin_set_dt(&led_green, 1);
	}

	led_state = !led_state;

	k_work_schedule(&led_work, K_SECONDS(1));
}

static int bat_led_init(void){
    if(!device_is_ready(led_red.port)) return -ENODEV;
    if(!device_is_ready(led_green.port)) return -ENODEV;

    gpio_pin_configure_dt(&led_red, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&led_green, GPIO_OUTPUT_INACTIVE);

    k_work_schedule(&led_work, K_NO_WAIT);

    return 0;
}

SYS_INIT(bat_led_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

//static void led_proc_thread(void *d0, void *d1, void *d2) {
/*static int led_proc_thread(void){
    //ARG_UNUSED(d0);
    //ARG_UNUSED(d1);
    //ARG_UNUSED(d2);

    if(!device_is_ready(led_red.port)) return;
    if(!device_is_ready(led_green.port)) return;

	int ret;
	ret = gpio_pin_configure_dt(&led_red, GPIO_OUTPUT_ACTIVE);
	if(ret < 0) return;

	ret = gpio_pin_configure_dt(&led_green, GPIO_OUTPUT_ACTIVE);
	if(ret < 0) return;

	gpio_pin_set_dt(&led_red, 1);
	gpio_pin_set_dt(&led_green, 1);
}

//K_THREAD_DEFINE(led_proc_id, 1024, led_proc_thread, NULL, NULL, NULL, K_LOWEST_APPLICATION_THREAD_PRIO, 0, 100);
SYS_INIT(led_proc_thread, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);