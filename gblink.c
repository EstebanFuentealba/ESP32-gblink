#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "gblink.h"

#define TAG "gblink"


struct gblink {
    gpio_num_t serin;
    gpio_num_t serout;
    gpio_num_t clk;

    uint8_t in;
    uint8_t out;
    uint8_t out_buf;
    bool out_buf_valid;
    uint8_t shift;
    uint8_t nobyte;
    gblink_clk_source source;
    gblink_mode mode;
    gblink_speed speed;

    TickType_t time;

    uint32_t bitclk_timeout_us;

    SemaphoreHandle_t packet_done;

    void (*callback)(void* cb_context, uint8_t in);
    void *cb_context;
};


void gblink_clk_source_set(void *handle, int source)
{
	struct gblink *gblink = handle;

	gblink->source = source;
	gblink->shift = 0;
}

void gblink_speed_set(void *handle, gblink_speed speed)
{
	struct gblink *gblink = handle;

	gblink->speed = speed;
}

/* default is set to 200 us */
void gblink_timeout_set(void *handle, uint32_t us)
{
	struct gblink *gblink = handle;

	gblink->bitclk_timeout_us = us;
}

void gblink_transfer(void *handle, uint8_t val)
{
	struct gblink *gblink = handle;

	/* This checks the value of gblink->shift which can change in the ISR.
	 * Because of that, disable interrupts when checking gblink->shift and
	 * setting gblink->out_buf_valid
	 * If shift is 0, we're between bytes and can safely set the out byte.
	 * If shift is nonzero, a byte is currently being transmitted. Set the
	 * out_buf and set out_buf_valid. When the ISR is finished writing the
	 * next byte it will check out_buf_valid and copy in out_buf.
	 *
	 * The correct/smart way of doing this would be a mutex rather than
	 * stopping the world.
	 *
	 * Realistically, this should only ever be called from the transfer
	 * complete callback. There are few situations outside of that which
	 * would make sense.
	 *
	 * Note that, this currently has no checks for if there is data already
	 * pending to be transmitted. Calling this back to back can cause data
	 * loss!
	 */
	if (gblink->shift == 0) {
		gblink->out = val;
		gblink->out_buf_valid = false;
	} else {
		gblink->out_buf = val;
		gblink->out_buf_valid = true;
	}
}

void gblink_nobyte_set(void *handle, uint8_t val)
{
	struct gblink *gblink = handle;
	gblink->nobyte = val;
}

static void gblink_shift_in(struct gblink *gblink)
{
    uint64_t curr_time = esp_timer_get_time();
    /* If we exceeded the bit clock timeout, reset all counters */
    if ((curr_time - gblink->time) > gblink->bitclk_timeout_us) {
        gblink->in = 0;
        gblink->shift = 0;
    }
    gblink->time = curr_time;

    gblink->in <<= 1;
    gblink->in |= gpio_get_level(gblink->serin);
    gblink->shift++;
    /* If 8 bits transferred, reset shift counter, call registered
     * callback, re-set nobyte in output buffer.
     */
    if (gblink->shift == 8) {
        gblink->shift = 0;

        /* Set up next out byte before calling the callback.
         * This is in case the callback itself sets a new out
         * byte which it will in most cases. It is up to the
         * main application at this time to ensure that
         * gblink_transfer() isn't called multiple times before
         * a byte has a chance to be sent out.
         */
        if (gblink->out_buf_valid) {
            gblink->out = gblink->out_buf;
            gblink->out_buf_valid = false;
        } else {
            gblink->out = gblink->nobyte;
        }
        gblink->callback(gblink->cb_context, gblink->in);
    }
}

static void gblink_shift_out(struct gblink *gblink)
{
    gpio_set_level(gblink->serout, !!(gblink->out & 0x80));
    gblink->out <<= 1;
}

static void gblink_clk_isr(void *context)
{
    struct gblink *gblink = context;
    if(gpio_get_level(gblink->clk)){
        /* Posedge Shift in data */
        gblink_shift_in(gblink);
    } else {
        /* Negedge shift out data */
        gblink_shift_out(gblink);
    }
}

struct gblink* gblink_alloc(struct gblink_def *gblink_def)
{
    struct gblink *gblink = malloc(sizeof(struct gblink));
    /* Allocate and zero struct */
	gblink = malloc(sizeof(struct gblink));
    /* Set struct values from function args */
	gblink->serin = gblink_def->pins->serin;
	gblink->serout = gblink_def->pins->serout;
	gblink->clk = gblink_def->pins->clk;
	// gblink->sd = gblink_def->pins->sd;
	gblink->source = gblink_def->source;
	gblink->speed = GBLINK_SPD_8192HZ;

    /* Set up timeout variables */
	gblink->bitclk_timeout_us = 200;
	gblink->time = esp_timer_get_time();

    /* Set up secondary callback */
	gblink->callback = gblink_def->callback;
	gblink->cb_context = gblink_def->cb_context;


    /* Set up pins */
	/* TODO: Currently assumes external clock source only */
	/* XXX: This might actually be open-drain on real GB hardware */

    esp_err_t result;
    gpio_config_t io_cfg;

    memset(&io_cfg, 0x0, sizeof(io_cfg));
    io_cfg.intr_type = GPIO_INTR_ANYEDGE;
    io_cfg.mode = GPIO_MODE_INPUT;
    io_cfg.pull_up_en = 0;
    io_cfg.pin_bit_mask = (1u << gblink->clk);

    result = gpio_config(&io_cfg);
    if(result != ESP_OK){
        ESP_LOGE(TAG, "gpio_config() failed for GPIO_SCLK");
    }

    io_cfg.intr_type = GPIO_INTR_DISABLE;
    io_cfg.mode = GPIO_MODE_INPUT;
    io_cfg.pull_up_en = 0;
    io_cfg.pin_bit_mask = (1u << gblink->serin);

    result = gpio_config(&io_cfg);
    if(result != ESP_OK){
        ESP_LOGE(TAG, "gpio_config() failed for GPIO_MOSI");
    }

    io_cfg.intr_type = GPIO_INTR_DISABLE;
    io_cfg.mode = GPIO_MODE_OUTPUT_OD;
    io_cfg.pull_up_en = 0;
    io_cfg.pin_bit_mask = (1u << gblink->serout);

    result = gpio_config(&io_cfg);
    if(result != ESP_OK){
        ESP_LOGE(TAG, "gpio_config() failed for GPIO_MISO");
    }


    result = gpio_install_isr_service(0);
    if(result != ESP_OK){
        ESP_LOGE(TAG, "[%s] gpio_install_isr_service() failed", __func__);
    }

    result = gpio_isr_handler_add(gblink->clk, gblink_clk_isr, gblink);
    if(result != ESP_OK){
        ESP_LOGE(TAG, "[%s] gpio_isr_handler_add() failed", __func__);
    }

    return gblink;
}

void gblink_free(void *handle)
{
    struct gblink *gblink = handle;
    /* Remove interrupt, set IO to sane state */
    gpio_isr_handler_remove(gblink->clk);
    gpio_reset_pin(gblink->serin);
    gpio_reset_pin(gblink->serout);
    gpio_reset_pin(gblink->clk);

    free(gblink);
}