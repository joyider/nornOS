/**
 * @file dht11.c
 * DHT11 single wire protocol, polling implementation.
 */

#include <util/delay.h>
#include "dht11.h"

/** Pulse measurement step in microseconds. */
#define STEP_US         2u
/** Longest pulse we accept, in microseconds. */
#define MAX_PULSE_US    120u
/** A high pulse longer than this is a 1 bit. */
#define ONE_THRESHOLD_US 50u

/**
 * Wait while the line has the given level.
 *   @return Elapsed time in microseconds, or 0xFF on timeout.
 */
static uint8_t waitWhile(const gpio_pin_t *pPin, bool level)
{
    uint8_t us = 0;

    while(gpio_read(pPin) == level)
    {
        _delay_us(STEP_US);
        us = (uint8_t)(us + STEP_US);
        if(us >= MAX_PULSE_US)
            return 0xFFu;
    }
    return us;
}

void dht11_startRequest(const gpio_pin_t *pPin)
{
    gpio_output(pPin);
    gpio_write(pPin, false);
}

dht11_status_t dht11_readResponse(const gpio_pin_t *pPin, dht11_reading_t *pReading)
{
    uint8_t data[5] = {0, 0, 0, 0, 0};
    uint8_t i;

    /* Release the line; the sensor answers within 20-40 us with 80 us low, 80 us high. */
    gpio_input(pPin, true);
    _delay_us(30);
    if(waitWhile(pPin, true) == 0xFFu)
        return DHT11_ERR_NO_RESPONSE;
    if(waitWhile(pPin, false) == 0xFFu  ||  waitWhile(pPin, true) == 0xFFu)
        return DHT11_ERR_NO_RESPONSE;

    /* 40 bits: each is 50 us low followed by 26-28 us (0) or 70 us (1) high. */
    for(i=0; i<40; ++i)
    {
        uint8_t high;

        if(waitWhile(pPin, false) == 0xFFu)
            return DHT11_ERR_TIMEOUT;
        high = waitWhile(pPin, true);
        if(high == 0xFFu)
            return DHT11_ERR_TIMEOUT;

        data[i >> 3] = (uint8_t)(data[i >> 3] << 1);
        if(high > ONE_THRESHOLD_US)
            data[i >> 3] |= 1u;
    }

    if((uint8_t)(data[0] + data[1] + data[2] + data[3]) != data[4])
        return DHT11_ERR_CHECKSUM;

    pReading->humidityPct = data[0];
    pReading->tempDeciC = (int16_t)((int16_t)(data[2] & 0x7Fu) * 10 + (data[3] & 0x0Fu));
    if(data[2] & 0x80u)
        pReading->tempDeciC = (int16_t)-pReading->tempDeciC;
    return DHT11_OK;
}
