#ifndef CO3009_I2C_ASYNC_H
#define CO3009_I2C_ASYNC_H
#include <stdint.h>
#include <stddef.h>

typedef uint32_t i2c_async_token_t;
typedef enum {
    I2C_ASYNC_ACCEPTED, I2C_ASYNC_BUSY, I2C_ASYNC_PENDING,
    I2C_ASYNC_COMPLETE, I2C_ASYNC_ERROR
} i2c_async_result_t;

/* Injected backend contract, no bus singleton implied:
 * All calls bounded/nonblocking, foreground-only, non-reentrant. No allocation.
 * Addresses are unshifted 7-bit; length>0; pointers valid; owner!=NULL and stable.
 * tx/rx return ACCEPTED + token, BUSY (bus occupied), or ERROR.
 * BUSY/ERROR borrow nothing and leave token unchanged. ACCEPTED borrows buffer
 * until poll/cancel returns COMPLETE or ERROR. TX must stay unchanged; caller
 * must not access RX while borrowed. No synchronous completion on submission.
 * poll returns PENDING, COMPLETE (all bytes), ERROR (including short transfer).
 * Terminal results release buffer AND retire token; no further ISR/DMA access.
 * cancel only matching owner/token: COMPLETE/ERROR terminal; PENDING means abort
 * in progress, caller must poll until terminal BEFORE reusing buffer/storage.
 * Invalid/stale/foreign tokens return ERROR without touching another owner.
 * Backend serializes all clients, uses generation-safe tokens, synchronizes
 * ISR/foreground state and eventually quiesces transfers/aborts. If unable to
 * quiesce, retain ownership and PENDING; never falsely release borrowed memory.
 * Client owns timeouts and requests cancellation. Context and owner outlive use.
 */
typedef struct {
    void *context;
    i2c_async_result_t (*transmit)(void *, const void *owner, uint8_t address_7bit,
        const uint8_t *, size_t, i2c_async_token_t *);
    i2c_async_result_t (*receive)(void *, const void *owner, uint8_t address_7bit,
        uint8_t *, size_t, i2c_async_token_t *);
    i2c_async_result_t (*poll)(void *, const void *owner, i2c_async_token_t);
    i2c_async_result_t (*cancel)(void *, const void *owner, i2c_async_token_t);
} i2c_async_t;
#endif /* CO3009_I2C_ASYNC_H */
