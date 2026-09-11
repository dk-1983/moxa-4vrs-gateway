#include <string.h>

#include "modbus/mbap_stream.h"

static unsigned char peek(const mbap_stream_t *stream, unsigned int offset)
{
    return stream->bytes[(stream->head + offset) % MBAP_RX_CAPACITY];
}

static int parse_available(mbap_stream_t *stream, mbap_adu_fn callback,
                           void *context)
{
    mbap_adu_t adu;
    unsigned int protocol;
    unsigned int mbap_length;
    unsigned int frame_length;
    unsigned int i;
    while (stream->count >= MBAP_HEADER_SIZE) {
        protocol = ((unsigned int)peek(stream, 2) << 8) | peek(stream, 3);
        mbap_length = ((unsigned int)peek(stream, 4) << 8) | peek(stream, 5);
        if (protocol != 0 || mbap_length < MBAP_LENGTH_MIN ||
            mbap_length > MBAP_LENGTH_MAX) {
            ++stream->malformed;
            stream->poisoned = 1;
            return -1;
        }
        frame_length = 6U + mbap_length;
        if (frame_length > MBAP_ADU_MAX) {
            ++stream->malformed;
            stream->poisoned = 1;
            return -1;
        }
        if (stream->count < frame_length)
            return 0;
        adu.transaction_id = (unsigned short)(((unsigned int)peek(stream, 0) << 8) |
                                               peek(stream, 1));
        adu.unit_id = peek(stream, 6);
        adu.pdu_length = mbap_length - 1U;
        for (i = 0; i < adu.pdu_length; ++i)
            adu.pdu[i] = peek(stream, 7U + i);
        if (callback != 0 && callback(context, &adu) != 0)
            return 1;
        stream->head = (stream->head + frame_length) % MBAP_RX_CAPACITY;
        stream->count -= frame_length;
        ++stream->frames;
    }
    return 0;
}

void mbap_stream_init(mbap_stream_t *stream)
{
    memset(stream, 0, sizeof(*stream));
}

mbap_result_t mbap_stream_feed(mbap_stream_t *stream,
                               const unsigned char *data,
                               unsigned int length,
                               mbap_adu_fn callback, void *context)
{
    unsigned int tail;
    unsigned int room;
    unsigned int chunk;
    unsigned int first;
    int parsed;
    if (stream == 0 || (data == 0 && length != 0) || stream->poisoned)
        return MBAP_MALFORMED;
    if (length > MBAP_RX_CAPACITY - stream->count) {
        ++stream->overflows;
        stream->poisoned = 1;
        return MBAP_OVERFLOW;
    }
    while (length != 0) {
        room = MBAP_RX_CAPACITY - stream->count;
        if (room == 0) {
            ++stream->overflows;
            stream->poisoned = 1;
            return MBAP_OVERFLOW;
        }
        chunk = length < room ? length : room;
        tail = (stream->head + stream->count) % MBAP_RX_CAPACITY;
        first = MBAP_RX_CAPACITY - tail;
        if (first > chunk)
            first = chunk;
        memcpy(stream->bytes + tail, data, first);
        if (chunk > first)
            memcpy(stream->bytes, data + first, chunk - first);
        stream->count += chunk;
        data += chunk;
        length -= chunk;
    }
    parsed = parse_available(stream, callback, context);
    if (parsed < 0)
        return MBAP_MALFORMED;
    return parsed > 0 ? MBAP_STOPPED : MBAP_OK;
}

unsigned int mbap_stream_buffered(const mbap_stream_t *stream)
{
    return stream != 0 ? stream->count : 0;
}
