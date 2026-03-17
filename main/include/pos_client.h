#ifndef POS_CLIENT_H
#define POS_CLIENT_H


#include "types.h"


/* Wire values are fixed — do not reorder. */
typedef enum {
    POS_CUSTOMERS_SEATED = 0,
    POS_ORDER_READY      = 1,
    POS_BILL_REQUESTED   = 2,
} pos_event_type;


/* 2-byte wire message, no framing. table_index must be < MAX_TABLES. */
typedef struct {
    uint8_t type;
    uint8_t table_index;
} __attribute__((packed)) pos_message;


/* Call once at startup. */
void pos_client_start(void);


/* Drain queued POS events into the table FSM. Call from the scheduler tick. */
void pos_client_drain_events(time_ms current_time_ms);


#endif // POS_CLIENT_H
