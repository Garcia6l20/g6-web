#pragma once

#include <g6/ws/header.hpp>

#include <g6/coro/sync_wait.hpp>
#include <g6/coro/task.hpp>

#include <g6/algoritm>

#include <algorithm>
#include <bit>
#include <span>
#include <stop_token>

#include <cassert>

namespace g6::ws {

    template<bool is_server, typename Socket>
    class connection;

    template<bool is_server, typename Socket>
    class message;

    template<bool is_server, typename Socket>
    inline task<message<is_server, Socket>> tag_invoke(tag_t<net::async_recv>, connection<is_server, Socket> &conn,
                                                       std::stop_token stop = {});

    template<bool is_server, typename Socket>
    task<size_t> tag_invoke(tag_t<net::async_send>, connection<is_server, Socket> &, std::span<std::byte const>);

    template<bool is_server, typename Socket>
    task<message<is_server, Socket>> tag_invoke(tag_t<net::async_recv>, connection<is_server, Socket> &conn,
                                                        std::stop_token stop);
                                                        
    template<bool is_server, typename Socket>
    class message {
        connection<is_server, Socket> &connection_;

        template<bool, typename>
        friend class connection;

        Socket &socket_;
        header header_;
        size_t current_payload_offset_ = 0;

        explicit message(connection<is_server, Socket> &conn) noexcept : connection_{conn}, socket_{conn.socket_} {}

        friend task<message<is_server, Socket>> tag_invoke<>(tag_t<net::async_recv>, connection<is_server, Socket> &conn,
                                                             std::stop_token stop);

        task<size_t> async_recv(std::span<std::byte> data) {
            ssize_t remaining_size = header_.payload_length - current_payload_offset_;
            if (remaining_size <= 0) {
                assert(!header_.fin);
                header_ = co_await header::receive(socket_);
                current_payload_offset_ = 0;
                remaining_size = header_.payload_length - current_payload_offset_;
                spdlog::debug("opcode: {}, fin: {}, len: {}", to_string(header_.opcode), header_.fin,
                              header_.payload_length);
                if (header_.opcode == op_code::connection_close) {
                    current_payload_offset_ = header_.payload_length;
                    if (header_.payload_length == 2) {
                        // get status
                        uint16_t status;
                        co_await net::async_recv(socket_, as_writable_bytes(std::span{&status, 1}));
                        header_.mask_body(as_writable_bytes(std::span{&status, 1}));
                        connection_.status_ = status_code(std::byteswap(status));
                        co_return 0;
                    } else {
                        connection_.status_ = status_code::closed_abnormally;
                        co_return 0;
                    }
                }
            }
            auto bytes_to_receive = std::min(data.size(), size_t(remaining_size));
            if (bytes_to_receive == 0) { co_return 0; }
            size_t rx_size = co_await net::async_recv(socket_, std::span{data.data(), bytes_to_receive});
            header_.mask_body(std::span{data.data(), rx_size});

            current_payload_offset_ += rx_size;

            spdlog::debug("received: {} bytes ({}/{})", rx_size, current_payload_offset_,
                          header_.payload_length);

            co_return rx_size;
        }

        friend task<size_t> tag_invoke(tag_t<net::async_recv>, message &req, std::span<std::byte> data) {
            return req.async_recv(data);
        }

        friend bool tag_invoke(tag_t<net::has_pending_data>, message &req) {
            return !req.header_.fin || req.current_payload_offset_ < req.header_.payload_length;
        }
    };

    template<bool is_server, typename Socket>
    class connection {
        template<bool, typename>
        friend class message;

        Socket socket_;
        net::ip_endpoint remote_endpoint_;
        const uint32_t ws_version;

        static uint32_t make_masking_key() noexcept {
            static std::mt19937 rng{size_t(time(nullptr) * getpid())};
            return uint32_t(rng());
        }

        status_code status_ = status_code::undefined;

    public:
        static constexpr uint32_t max_ws_version_ = 13;
        auto const &remote_endpoint() const noexcept { return remote_endpoint_; }

        connection(connection const &) = delete;
        connection(connection &&) noexcept = default;

        ~connection() noexcept = default;

        constexpr bool operator==(connection const &other) const noexcept { return socket_ == other.socket_; }
        constexpr auto operator<=>(connection const &) const noexcept = default;

        auto status() const noexcept { return status_; }

    private:
        template<bool is_server_, typename Socket_>
        friend task<message<is_server_, Socket_>> tag_invoke(tag_t<net::async_recv>,
                                                             connection<is_server_, Socket_> &conn, std::stop_token);

        friend task<size_t> tag_invoke(tag_t<net::async_send>, connection &conn, std::span<std::byte const> data) {
            std::array<std::byte, header::max_header_size> header_data;
            header h{
                .fin = true,
                .opcode = op_code::text_frame,
                .payload_length = data.size(),
            };
            if constexpr (not is_server) {
                h.mask = true;
                h.masking_key = make_masking_key();
                co_await h.send(conn.socket_);
                std::array<std::byte, 128> masked_data;
                size_t remaining_size = data.size();
                while (remaining_size) {
                    const size_t tmp_size = tl::min(data.size(), masked_data.size(), remaining_size);
                    std::memcpy(masked_data.data(), data.data() + data.size() - remaining_size, tmp_size);
                    h.mask_body(masked_data);
                    size_t tx_size = co_await net::async_send(conn.socket_, std::span{masked_data.data(), tmp_size});
                    remaining_size -= tx_size;
                    spdlog::debug("sent: {} bytes ({}/{})", tx_size, data.size() - remaining_size, data.size());
                }
                co_return data.size();
            } else {
                co_await h.send(conn.socket_);
                co_return co_await net::async_send(conn.socket_, data);
            }
        }

        friend task<> tag_invoke(tag_t<net::async_close>, connection& self, status_code reason = status_code::normal_closure) {            
            uint16_t status = uint16_t(reason);
            header h{
                .fin = true,
                .opcode = op_code::connection_close,
                .mask = not is_server,
                .payload_length = sizeof(status),
            };
            if constexpr (not is_server) { h.masking_key = self.make_masking_key(); }
            co_await h.send(self.socket_);
            // status
            status = std::byteswap(status);
            h.mask_body(as_writable_bytes(std::span{&status, 1}));
            co_await net::async_send(self.socket_, std::span{&status, 1});
            self.status_ = reason;
        }

        task<message<is_server, Socket>> await() { co_return message<is_server, Socket>{*this}; }

        friend bool tag_invoke(tag_t<net::has_pending_data>, connection &self) {
            return self.status_ == status_code::undefined;
        }

    protected:
        explicit connection(Socket &&socket, net::ip_endpoint const &remote_endpoint,
                            uint32_t version = max_ws_version_) noexcept
            : socket_{std::forward<Socket>(socket)}, remote_endpoint_{remote_endpoint}, ws_version{version} {}
    };

    template<bool is_server, typename Socket>
    task<message<is_server, Socket>> tag_invoke(tag_t<net::async_recv>, connection<is_server, Socket> &conn,
                                                std::stop_token stop) {
        co_return co_await conn.await();
    }
}// namespace g6::ws
