#ifndef DHT11_INCLUDED
#define DHT11_INCLUDED
/**
 * @file dht11.h
 * DHT11 temperature and humidity sensor on a single data pin.
 *
 * The protocol needs an 18 ms start pulse followed by a 4 ms bit stream. To avoid a busy
 * wait of 18 ms, reading is split: dht11_startRequest pulls the line low, the caller waits
 * at least 18 ms (rtos_delay), then dht11_readResponse releases the line and decodes the
 * bits. The decoding busy waits for about 4 ms with interrupts enabled; pulse widths are
 * measured by polling, with a wide margin between the 26 us "0" and 70 us "1" pulses.
 * A tick interrupt landing in a pulse can stretch it by about 10 us, which is harmless.
 */

#include <stdint.h>
#include "gpio.h"

typedef enum dht11_status
{
    DHT11_OK = 0,
    DHT11_ERR_NO_RESPONSE,  /**< Sensor did not answer the start pulse. */
    DHT11_ERR_TIMEOUT,      /**< A pulse took too long. */
    DHT11_ERR_CHECKSUM
} dht11_status_t;

typedef struct dht11_reading
{
    int16_t tempDeciC;      /**< Temperature in tenths of a degree Celsius. */
    uint8_t humidityPct;    /**< Relative humidity in percent. */
} dht11_reading_t;

/** Pull the data line low. Wait at least 18 ms before calling dht11_readResponse. */
void dht11_startRequest(const gpio_pin_t *pPin);

/** Release the line and read the 40 bit answer. */
dht11_status_t dht11_readResponse(const gpio_pin_t *pPin, dht11_reading_t *pReading);

#endif  /* DHT11_INCLUDED */
