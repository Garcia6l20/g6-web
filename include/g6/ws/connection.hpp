#pragma once

#include <g6/ws/header.hpp>

#include <unifex/span.hpp>

namespace g6::ws {

    template<bool is_server, typename Socket>
    class connection;
}

namespace g6::net {
    template<bool is_server, typename Socket>
    task<typename ws::connection<is_server, Socket>::request> tag_invoke(tag_t<net::async_recv>,
                                                                         ws::connection<is_server, Socket> &conn);
}

namespace g6::ws {


    template<bool is_server, typename Socket>
    class connection
    {
    public:
        static constexpr uint32_t max_ws_version_ = 13;
        const uint32_t ws_version;
        auto const &remote_endpoint() const noexcept { return remote_endpoint_; }


        connection(connection const &) = delete;
        connection(connection &&) noexcept = default;


        ~connection() noexcept = default;

        constexpr bool operator==(connection const &other) const noexcept { return socket_ == other.socket_; }
        constexpr auto operator<=>(connection const &) const noexcept = default;

    private:
        Socket socket_;
        std::array<std::byte, 1024> data_{};
        net::ip_endpoint remote_endpoint_;

        class request
        {
            connection &connection_;
            unifex::span<std::byte> buffer_;
            header header_;
            size_t current_payload_offset_ = 0;

            explicit request(connection &conn, size_t received_size) noexcept
                : connection_{conn}, buffer_{connection_.data_}, header_{header::parse(
                                                                     span{buffer_.data(), received_size})} {
#ifdef G6_WEB_DEBUG
                spdlog::debug("ws::request<{}>:\n"
                              " - received {} bytes\n"
                              " - payload_length={} bytes\n"
                              " - payload_offset={} bytes",
                              is_server ? "server" : "client", received_size, header_.payload_length,
                              header_.payload_offset);
                spdlog::debug("ws::request<{}>: first byte={}, buffer=0x{}", is_server ? "server" : "client",
                              (uint8_t) buffer_[0], (void *) buffer_.data());
#endif
                copy_body(span{buffer_.data(), received_size});
            }

            void copy_body(span<std::byte const> buffer) noexcept {
                if (header_.mask) {
                    std::byte masking_key[4]{};
                    std::memcpy(masking_key, &header_.masking_key, 4);
                    for (size_t ii = 0; ii < header_.payload_length; ++ii) {
                        buffer_[current_payload_offset_++] =
                            buffer[header_.payload_offset + ii] xor masking_key[ii % 4];
                    }
                } else {
                    std::memcpy(buffer_.data(), &buffer[header_.payload_offset], header_.payload_length);
                    current_payload_offset_ += header_.payload_length;
                }
            }

            friend task<request> net::tag_invoke<is_server, Socket>(tag_t<net::async_recv>, connection &conn);

            friend task<span<const std::byte>> tag_invoke(tag_t<net::async_recv>, request &req) {
                uint32_t payload_offset = std::exchange(req.current_payload_offset_, 0);
                co_return as_bytes(span{req.buffer_.data(), payload_offset});
                //                    auto &socket = request.session_.socket();
                //                    std::array<std::byte, 1024> buffer{};
                //                    auto bytes_received = co_await net::async_recv(socket, buffer);
                //                    request.header_ = header::parse(span{buffer.data(), bytes_received});
                //                    request.copy_body(as_bytes(span{buffer}));
                //                    co_return as_bytes(span{request.buffer_.data(), bytes_received});
            }

            friend bool tag_invoke(tag_t<net::has_pending_data>, request &req) {
                return !req.header_.fin || req.current_payload_offset_ != 0;
            }
        };

        friend task<request> net::tag_invoke<is_server, Socket>(tag_t<net::async_recv>, connection &conn);

        friend task<size_t> tag_invoke(tag_t<net::async_send>, connection &conn, span<std::byte const> data) {
            size_t data_offset = 0;
            while (data_offset < data.size()) {
                header h{
                    .opcode = op_code::text_frame,
                };
                size_t payload_size = std::min(conn.data_.size(), data.size() - data_offset);
                auto [send_size, payload_len] = h.calc_payload_size(payload_size, conn.data_.size());
                h.fin = (data_offset + payload_len >= data.size());
                h.serialize(span{conn.data_});
                std::memcpy(&conn.data_[h.payload_offset], data.data() + data_offset, h.payload_length);
                data_offset += h.payload_length;
#ifdef G6_WEB_DEBUG
                spdlog::debug("ws::connection<{}>: send_size={} bytes", is_server ? "server" : "client", send_size);
                spdlog::debug("ws::connection<{}>: payload_length={} bytes", is_server ? "server" : "client",
                              h.payload_length);
                spdlog::debug("ws::connection<{}>: payload_offset={} bytes", is_server ? "server" : "client",
                              h.payload_offset);
                spdlog::debug("ws::connection<{}>: first byte={}", is_server ? "server" : "client",
                              (uint8_t) conn.data_[0]);
#endif
                co_await net::async_send(conn.socket_, as_bytes(span{conn.data_.data(), send_size}));
            }
            //            conn.socket_.close_send();
            co_return data_offset;
        }

    protected:
        explicit connection(Socket &&socket, net::ip_endpoint const &remote_endpoint,
                            uint32_t version = max_ws_version_) noexcept
            : socket_{std::forward<Socket>(socket)}, remote_endpoint_{remote_endpoint}, ws_version{version} {}
    };

}// namespace g6::ws

namespace g6::net {
    template<bool is_server, typename Socket>
    task<typename ws::connection<is_server, Socket>::request> tag_invoke(tag_t<net::async_recv>,
                                                                         ws::connection<is_server, Socket> &conn) {
        auto &socket = conn.socket_;
#ifdef G6_WEB_DEBUG
        spdlog::debug("ws::connection<{}>: buffer=0x{}", is_server ? "server" : "client", (void *) conn.data_.data());
#endif
        auto bytes_received = co_await net::async_recv(socket, span{conn.data_});
        if (bytes_received == 0) { throw std::system_error(std::make_error_code(std::errc::connection_reset)); }
        co_return typename ws::connection<is_server, Socket>::request{conn, bytes_received};
    }
}// namespace g6::net
