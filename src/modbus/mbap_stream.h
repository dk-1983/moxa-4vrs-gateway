#ifndef FOURVRS_MBAP_STREAM_H
#define FOURVRS_MBAP_STREAM_H

#define MBAP_HEADER_SIZE 7U
#define MBAP_PDU_MAX 253U
#define MBAP_ADU_MAX (MBAP_HEADER_SIZE + MBAP_PDU_MAX)
#define MBAP_LENGTH_MIN 2U
#define MBAP_LENGTH_MAX 254U
#define MBAP_RX_CAPACITY (MBAP_ADU_MAX * 4U)

typedef enum mbap_result {
    MBAP_OK = 0,
    MBAP_OVERFLOW,
    MBAP_MALFORMED,
    MBAP_STOPPED
} mbap_result_t;

typedef struct mbap_adu {
    unsigned short transaction_id;
    unsigned char unit_id;
    unsigned int pdu_length;
    unsigned char pdu[MBAP_PDU_MAX];
} mbap_adu_t;

typedef int (*mbap_adu_fn)(void *context, const mbap_adu_t *adu);

typedef struct mbap_stream {
    unsigned char bytes[MBAP_RX_CAPACITY];
    unsigned int head;
    unsigned int count;
    unsigned int frames;
    unsigned int malformed;
    unsigned int overflows;
    int poisoned;
} mbap_stream_t;

void mbap_stream_init(mbap_stream_t *stream);
mbap_result_t mbap_stream_feed(mbap_stream_t *stream,
                               const unsigned char *data,
                               unsigned int length,
                               mbap_adu_fn callback, void *context);
unsigned int mbap_stream_buffered(const mbap_stream_t *stream);

#endif
