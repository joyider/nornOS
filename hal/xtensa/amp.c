/**
 * @file amp.c
 * Message link between the two cores through the shared memory window.
 */

#include <string.h>
#include "amp.h"
#include "rtos_assert.h"

#define AMP_MAGIC   0x414D5031ul        /* "AMP1" */

typedef struct amp_ring
{
    volatile uint16_t head;             /**< Next slot to read (consumer). */
    volatile uint16_t tail;             /**< Next slot to write (producer). */
    amp_msg_t slot[AMP_RING_SLOTS];
} amp_ring_t;

typedef struct amp_shared
{
    volatile uint32_t magic;
    amp_ring_t toCore1;
    amp_ring_t toCore0;
} amp_shared_t;

extern uint8_t _shared_start[];
extern uint8_t _shared_size[];

static amp_shared_t *_pShared;
static amp_ring_t *_pRx, *_pTx;
static rtos_sem_t _semRx = RTOS_SEM_INIT_BINARY(0);

#define CPU_INT_CROSS   13u

static inline void barrier(void)
{
    __asm__ __volatile__ ("memw" : : : "memory");
}

static void crossCoreIsr(void)
{
    rtos_port_ackCrossCore();
    rtos_semGiveFromIsr(&_semRx);
}

void amp_init(void)
{
    RTOS_ASSERT((uintptr_t)_shared_size >= sizeof(amp_shared_t));
    _pShared = (amp_shared_t *)_shared_start;
    if(rtos_port_coreId() == 0)
    {
        memset(_pShared, 0, sizeof(*_pShared));
        barrier();
        _pShared->magic = AMP_MAGIC;
        _pRx = &_pShared->toCore0;
        _pTx = &_pShared->toCore1;
        rtos_port_attachInterrupt(CPU_INT_CROSS, RTOS_PORT_SRC_FROM_CPU0, crossCoreIsr);
        RTOS_ASSERT(amp_core1_image != NULL  &&  &amp_core1_image_size != NULL);
        rtos_port_startAppCpu(amp_core1_image, amp_core1_image_size);
    }
    else
    {
        while(_pShared->magic != AMP_MAGIC)
            ;
        _pRx = &_pShared->toCore1;
        _pTx = &_pShared->toCore0;
        rtos_port_attachInterrupt(CPU_INT_CROSS, RTOS_PORT_SRC_FROM_CPU1, crossCoreIsr);
    }
}

bool amp_send(const amp_msg_t *pMsg)
{
    const uint16_t tail = _pTx->tail;
    const uint16_t next = (uint16_t)((tail + 1u) % AMP_RING_SLOTS);

    if(next == _pTx->head)
        return false;
    _pTx->slot[tail] = *pMsg;
    barrier();
    _pTx->tail = next;
    rtos_port_signalOtherCore();
    return true;
}

uint16_t amp_pending(void)
{
    return (uint16_t)((_pRx->tail + AMP_RING_SLOTS - _pRx->head) % AMP_RING_SLOTS);
}

bool amp_receive(amp_msg_t *pMsg, rtos_tick_t timeout)
{
    for(;;)
    {
        const uint16_t head = _pRx->head;
        if(head != _pRx->tail)
        {
            barrier();
            *pMsg = _pRx->slot[head];
            barrier();
            _pRx->head = (uint16_t)((head + 1u) % AMP_RING_SLOTS);
            return true;
        }
        if(!rtos_semTake(&_semRx, timeout))
            return false;
    }
}
