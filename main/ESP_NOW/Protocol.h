#ifndef __DD_PROTOCOL_H__
#define __DD_PROTOCOL_H__
/*
 * ESP-NOW wire format shared by dd-generator and dd-detector. Keep both copies identical.
 *
 * Handshake: the generator sends HELLO (broadcast until it knows the detector, then unicast
 * every HELLO_INTERVAL_MS as a keepalive). The detector answers the first HELLO with
 * HELLO_ACK and then streams DATA, stopping STREAM_TIMEOUT_MS after the last HELLO.
 *
 * Legacy frames, which are plain text rather than these structs:
 *   "Ov" - threshold exceeded, starts the generator. Independent of the handshake.
 *   "HH" - detector boot announcement. Counted as traffic, never starts the generator.
 */
#include <stdint.h>

namespace dd_proto
{
    constexpr uint8_t WIFI_CHANNEL = 1; // Both devices must stay on the same channel.
    constexpr uint8_t VERSION = 1;
    constexpr uint32_t HELLO_INTERVAL_MS = 2000;
    constexpr uint32_t STREAM_TIMEOUT_MS = 6000;

    enum Type : uint8_t
    {
        TYPE_HELLO = 1,
        TYPE_HELLO_ACK = 2,
        TYPE_DATA = 3,
    };

    struct __attribute__((packed)) Header
    {
        char magic[2]; // "DD"
        uint8_t version;
        uint8_t type;
        uint32_t seq; // Per-sender counter, used to detect lost DATA frames.
    };

    // Payload of both HELLO_ACK and DATA.
    struct __attribute__((packed)) DataFrame
    {
        Header header;
        int16_t db_x10;        // Current level in 0.1 dB.
        int16_t threshold_x10; // Configured threshold in 0.1 dB.
        uint32_t total_over;   // Lifetime threshold crossings.
    };
}

#endif
