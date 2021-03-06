/** @file cppcoro/ws/server.hpp
 * @author Sylvain Garcia <garcia.6l20@gmail.com>
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <limits>
#include <span>
#include <tuple>

namespace g6::ws {
    enum class op_code : uint8_t
    {
        continuation_frame = 0x00,
        text_frame = 0x01,
        binary_frame = 0x02,
        // 0x03-07 reserved
        connection_close = 0x08,
        ping = 0x09,
        pong = 0x0A,
    };

    enum class status_code : uint16_t
    {
        normal_closure = 1000,
        going_away = 1001,
        protocol_error = 1002,
        cannot_accept = 1003,
        // 1004 - reserved
        no_status_code = 1005,
        closed_abnormally = 1006,
        inconsistent_message = 1007,
        policy_violation = 1008,
        too_big = 1009,
        no_extension = 1010,
        unexpected_condition = 1011,
        tls_handshake_failure = 1015,
    };

    struct header {
        bool fin = false;
        bool rsv1 = false, rsv2 = false, rsv3 = false;
        op_code opcode = op_code::continuation_frame;
        bool mask = false;
        uint64_t payload_length = 0;
        uint32_t masking_key = 0;
        size_t payload_offset = 5;

        static constexpr size_t max_header_size = 14;

        static header parse(std::span<std::byte const> buffer) {
            header h{};

            h.fin = (buffer[0] & std::byte{0b1000'0000}) != std::byte{0};
            h.rsv1 = (buffer[0] & std::byte{0b0100'0000}) != std::byte{0};
            h.rsv2 = (buffer[0] & std::byte{0b0010'0000}) != std::byte{0};
            h.rsv3 = (buffer[0] & std::byte{0b0001'0000}) != std::byte{0};
            h.opcode = ws::op_code(buffer[0] & std::byte{0b0000'1111});

            h.mask = (buffer[1] & std::byte{0b1000'0000}) != std::byte{0};
            h.payload_length = uint64_t(buffer[1] & std::byte{0b0111'1111});
            size_t mask_offset = 2;
            if (h.payload_length == 126) {
                constexpr size_t index = 2;
                static_assert(std::is_trivially_copyable_v<std::byte>);
                uint16_t len = 0;
                std::memcpy(&len, &buffer[2], 2);
                h.payload_length = ntohs(len);
                mask_offset = 4;
            } else if (h.payload_length == 127) {
                uint64_t _payload_length = 0;
                std::memcpy(&_payload_length, &buffer[2], 8);
                h.payload_length = be64toh(_payload_length);
                mask_offset = 10;
            }
            if (h.mask) {
                std::memcpy(&h.masking_key, &buffer[mask_offset], 4);
                mask_offset += 4;
            }
            h.payload_offset = mask_offset;
            return h;
        }

        void update_payload_offset() {
            size_t mask_offset = 2;
            if (payload_length >= 127 && payload_length <= std::numeric_limits<uint16_t>::max()) {
                mask_offset = 4;
            } else if (payload_length > std::numeric_limits<uint16_t>::max()) {
                mask_offset = 10;
            }
            if (mask) { mask_offset += 4; }
            payload_offset = mask_offset;
        }

        std::tuple<size_t, size_t> calc_payload_size(size_t requested_payload_sz, size_t max_sz) {
            size_t to_send = 0;
            payload_length = requested_payload_sz;
            do {
                if (to_send > max_sz) { payload_length -= (to_send - max_sz); }
                update_payload_offset();
                to_send = payload_length + payload_offset;
            } while (to_send > max_sz);
            return {to_send, payload_length};
        }

        void serialize(span<std::byte> buffer) {
            size_t mask_offset = 2;
            std::memset(&buffer[0], 0, 14);
            buffer[0] = std::byte(fin << 7u) | std::byte(rsv1 << 6u) | std::byte(rsv2 << 5u) | std::byte(rsv3 << 4u)
                      | (std::byte(opcode) & std::byte(0b0000'1111));
            if (payload_length < 126) {
                buffer[1] = std::byte(mask << 7u) | (std::byte(0b0111'1111) & std::byte(payload_length));
            } else if (payload_length < std::numeric_limits<uint16_t>::max()) {
                buffer[1] = std::byte(mask << 7u) | std::byte(0b0111'1110);
                uint16_t len = htons(payload_length);
                std::memcpy(&buffer[2], &len, 2);
                mask_offset = 4;
            } else {
                buffer[1] = std::byte(mask << 7u) | std::byte(0b0111'1111);
                uint64_t _payload_length = htobe64(payload_length);
                std::memcpy(&buffer[2], &_payload_length, 8);
                mask_offset = 10;
            }
            if (mask) {
                std::memcpy(&buffer[mask_offset], &masking_key, 4);
                mask_offset += 4;
            }
            payload_offset = mask_offset;
        }
    };
}// namespace g6::ws
