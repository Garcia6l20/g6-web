#pragma once

#include <g6/ws/header.hpp>

#include <g6/coro/task.hpp>

#include <span>
#include <stop_token>
namespace g6::ws {

    template<bool is_server, typename Socket>
    class connection;

    template<bool is_server, typename Socket>
    class connection_request;

    template<bool is_server, typename Socket>
    inline task<connection_request<is_server, Socket>>
    tag_invoke(tag_t<net::async_recv>, connection<is_server, Socket> &conn, std::stop_token stop = {});

    template<bool is_server, typename Socket>
    task<size_t> tag_invoke(tag_t<net::async_send>, connection<is_server, Socket> &, std::span<std::byte const>);

    template<bool is_server, typename Socket>
    class connection_request {
        connection<is_server, Socket> &connection_;
        std::span<std::byte> buffer_;
        header header_;
        size_t current_payload_offset_ = 0;

        explicit connection_request(connection<is_server, Socket> &conn, size_t received_size) noexcept
            : connection_{conn}, buffer_{connection_.data_}, header_{header::parse(
                                                                 std::span{buffer_.data(), received_size})} {
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
            copy_body(std::span{buffer_.data(), received_size});
        }

        void copy_body(std::span<std::byte const> buffer) noexcept {
            if (header_.mask) {
                std::byte masking_key[4]{};
                std::memcpy(masking_key, &header_.masking_key, 4);
                for (size_t ii = 0; ii < header_.payload_length; ++ii) {
                    buffer_[current_payload_offset_++] = buffer[header_.payload_offset + ii] xor masking_key[ii % 4];
                }
            } else {
                std::memcpy(buffer_.data(), &buffer[header_.payload_offset], header_.payload_length);
                current_payload_offset_ += header_.payload_length;
            }
        }

        template<bool is_server_, typename Socket_>
        friend task<connection_request<is_server_, Socket_>>
        tag_invoke(tag_t<net::async_recv>, connection<is_server_, Socket_> &conn, std::stop_token);

        friend task<std::span<const std::byte>> tag_invoke(tag_t<net::async_recv>, connection_request &req) {
            size_t payload_offset = std::exchange(req.current_payload_offset_, 0);
            co_return as_bytes(std::span{req.buffer_.data(), payload_offset});
            //                    auto &socket = request.session_.socket();
            //                    std::array<std::byte, 1024> buffer{};
            //                    auto bytes_received = co_await
            //                    net::async_recv(socket, buffer); request.header_ =
            //                    header::parse(std::span{buffer.data(),
            //                    bytes_received});
            //                    request.copy_body(as_bytes(std::span{buffer}));
            //                    co_return as_bytes(std::span{request.buffer_.data(),
            //                    bytes_received});
        }

        friend bool tag_invoke(tag_t<net::has_pending_data>, connection_request &req) {
            return !req.header_.fin || req.current_payload_offset_ != 0;
        }
    };

    template<bool is_server, typename Socket>
    class connection {
        friend class connection_request<is_server, Socket>;

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

        template<bool is_server_, typename Socket_>
        friend task<connection_request<is_server_, Socket_>>
        tag_invoke(tag_t<net::async_recv>, connection<is_server_, Socket_> &conn, std::stop_token);

        friend task<size_t> tag_invoke(tag_t<net::async_send>, connection &conn, std::span<std::byte const> data) {
            size_t data_offset = 0;
            while (data_offset < data.size()) {
                header h{
                    .opcode = op_code::text_frame,
                };
                size_t payload_size = std::min(conn.data_.size(), data.size() - data_offset);
                auto [send_size, payload_len] = h.calc_payload_size(payload_size, conn.data_.size());
                h.fin = (data_offset + payload_len >= data.size());
                h.serialize(std::span{conn.data_});
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
                co_await net::async_send(conn.socket_, as_bytes(std::span{conn.data_.data(), send_size}));
            }
            //            conn.socket_.close_send();
            co_return data_offset;
        }

    protected:
        explicit connection(Socket &&socket, net::ip_endpoint const &remote_endpoint,
                            uint32_t version = max_ws_version_) noexcept
            : socket_{std::forward<Socket>(socket)}, remote_endpoint_{remote_endpoint}, ws_version{version} {}
    };

    template<bool is_server, typename Socket>
    task<connection_request<is_server, Socket>> tag_invoke(tag_t<net::async_recv>, connection<is_server, Socket> &conn,
                                                           std::stop_token stop) {
        auto &socket = conn.socket_;
#ifdef G6_WEB_DEBUG
        spdlog::debug("ws::connection<{}>: buffer=0x{}", is_server ? "server" : "client", (void *) conn.data_.data());
#endif
        auto bytes_received = co_await net::async_recv(socket, std::span{conn.data_}, stop);
        if (bytes_received == 0) { throw std::system_error(std::make_error_code(std::errc::connection_reset)); }
        co_return connection_request<is_server, Socket>{conn, bytes_received};
    }
}// namespace g6::ws
