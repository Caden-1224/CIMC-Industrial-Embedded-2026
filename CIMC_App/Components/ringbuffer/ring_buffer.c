#include "ring_buffer.h"
#include <stddef.h>

void ring_buffer_init(ring_buffer_t *ring, uint8_t *buffer, uint32_t size)
{
    if (ring == NULL) {
        return;
    }

    ring->buffer = buffer;
    ring->size = size;
    ring->head = 0U;
    ring->tail = 0U;
}

uint32_t ring_buffer_available(const ring_buffer_t *ring)
{
    if ((ring == NULL) || (ring->buffer == NULL) || (ring->size == 0U)) {
        return 0U;
    }

    if (ring->head >= ring->tail) {
        return ring->head - ring->tail;
    }

    return ring->size - ring->tail + ring->head;
}

uint32_t ring_buffer_free(const ring_buffer_t *ring)
{
    if ((ring == NULL) || (ring->buffer == NULL) || (ring->size == 0U)) {
        return 0U;
    }

    return ring->size - ring_buffer_available(ring) - 1U;
}

bool ring_buffer_write(ring_buffer_t *ring, const uint8_t *data, uint32_t len)
{
    uint32_t i;

    if ((ring == NULL) || (data == NULL)) {
        return false;
    }
    if (ring_buffer_free(ring) < len) {
        return false;
    }

    for (i = 0U; i < len; i++) {
        ring->buffer[ring->head] = data[i];
        ring->head = (ring->head + 1U) % ring->size;
    }

    return true;
}

uint32_t ring_buffer_peek(const ring_buffer_t *ring, uint8_t *data, uint32_t len)
{
    uint32_t available;
    uint32_t to_read;
    uint32_t tail;
    uint32_t i;

    if ((ring == NULL) || (data == NULL) || (ring->buffer == NULL) || (ring->size == 0U)) {
        return 0U;
    }

    available = ring_buffer_available(ring);
    to_read = (len < available) ? len : available;
    tail = ring->tail;

    for (i = 0U; i < to_read; i++) {
        data[i] = ring->buffer[tail];
        tail = (tail + 1U) % ring->size;
    }

    return to_read;
}

uint32_t ring_buffer_read(ring_buffer_t *ring, uint8_t *data, uint32_t len)
{
    uint32_t available;
    uint32_t to_read;
    uint32_t i;

    if ((ring == NULL) || (data == NULL) || (ring->buffer == NULL) || (ring->size == 0U)) {
        return 0U;
    }

    available = ring_buffer_available(ring);
    to_read = (len < available) ? len : available;

    for (i = 0U; i < to_read; i++) {
        data[i] = ring->buffer[ring->tail];
        ring->tail = (ring->tail + 1U) % ring->size;
    }

    return to_read;
}

void ring_buffer_drop(ring_buffer_t *ring, uint32_t len)
{
    uint32_t available;
    uint32_t to_drop;

    if ((ring == NULL) || (ring->buffer == NULL) || (ring->size == 0U)) {
        return;
    }

    available = ring_buffer_available(ring);
    to_drop = (len < available) ? len : available;
    ring->tail = (ring->tail + to_drop) % ring->size;
}

void ring_buffer_clear(ring_buffer_t *ring)
{
    if (ring == NULL) {
        return;
    }

    ring->head = 0U;
    ring->tail = 0U;
}
