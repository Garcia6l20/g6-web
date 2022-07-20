#pragma once

#include <g6/ws/header.hpp>

#include <g6/coro/sync_wait.hpp>
#include <g6/coro/task.hpp>

#include <g6/algoritm>
#include <g6/logging.hpp>

#include <algorithm>
#include <bit>
#include <random>
#include <span>
#include <stop_token>

#include <cassert>

#include <spdlog/spdlog.h>

namespace g6::ws {

    template<bool is_server, typename Socket>
    class connection;

    template<bool is_server, typename Socket>
    class rx_message;

    template<bool is_server, typename Socket>
    inline task<rx_message<is_server, Socket>> tag_invoke(tag_t<net::async_recv>, connection<is_server, Socket> &conn,
                                                          std::stop_token stop = {});

    template<bool is_server, typename Socket>
    task<size_t> tag_invoke(tag_t<net::async_send>, connection<is_server, Socket> &, std::span<std::byte const>);

    namespace details {
        inline uint32_t make_masking_key() noexcept {
            static std::mt19937 rng{size_t(time(nullptr) * getpid())};
            return uint32_t(rng());
        }
    }// namespace details

    template<bool is_server, typename Socket>
    class rx_message {
        connection<is_server, Socket> &connection_;

        template<bool, typename>
        friend class connection;

        Socket &socket_;
        header header_;
        size_t current_payload_offset_ = 0;

        explicit rx_message(connection<is_server, Socket> &conn) noexcept : connection_{conn}, socket_{conn.socket_} {}

        friend task<rx_message<is_server, Socket>>
        tag_invoke<>(tag_t<net::async_recv>, connection<is_server, Socket> &conn, std::stop_token stop);

        task<size_t> async_recv(std::span<std::byte> data) {
            ssize_t remaining_size = header_.payload_length - current_payload_offset_;
            if (remaining_size <= 0) {
                assert(!header_.fin);
            __get_header:
                header_ = co_await header::receive(socket_);
                current_payload_offset_ = 0;
                remaining_size = header_.payload_length - current_payload_offset_;
                connection_.debug("opcode: {}, fin: {}, len: {}", to_string(header_.opcode), header_.fin,
                              header_.payload_length);
                if (header_.opcode == op_code::connection_close) {
                    current_payload_offset_ = header_.payload_length;
                    uint16_t status;
                    if (header_.payload_length == 2) {
                        // get status
                        co_await net::async_recv(socket_, as_writable_bytes(std::span{&status, 1}));
                        header_.mask_body(as_writable_bytes(std::span{&status, 1}));
                        connection_.status_ = status_code(byteswap(status));
                    } else {
                        connection_.status_ = status_code::closed_abnormally;
                        status = byteswap(uint16_t(status_code::closed_abnormally));
                    }
                    if (not connection_.close_sent_) {
                        header h{
                            .fin = true,
                            .opcode = op_code::connection_close,
                            .mask = not is_server,
                            .payload_length = sizeof(status),
                        };
                        if constexpr (not is_server) { h.masking_key = details::make_masking_key(); }
                        co_await h.send(socket_);
                        co_await net::async_send(socket_, as_bytes(std::span{&status, 1}));
                        connection_.close_sent_ = true;
                    }
                    throw std::system_error{ws::make_error_code(connection_.status_)};
                } else if (header_.opcode == op_code::ping) {
                    // send pong response
                    header pong_h{
                        .fin = true,
                        .opcode = op_code::pong,
                        .mask = not is_server,
                        .masking_key = is_server ? 0 : details::make_masking_key(),
                    };
                    std::byte body_data[128];
                    std::span body{body_data, header_.payload_length};
                    co_await net::async_recv(socket_, body);
                    pong_h.payload_length = header_.payload_length;
                    co_await pong_h.send(socket_);
                    pong_h.mask_body(body);
                    co_await net::async_send(socket_, body);
                    goto __get_header;
                }
            }
            auto bytes_to_receive = std::min(data.size(), size_t(remaining_size));
            if (bytes_to_receive == 0) { co_return 0; }
            size_t rx_size = co_await net::async_recv(socket_, std::span{data.data(), bytes_to_receive});
            header_.mask_body(std::span{data.data(), rx_size});

            current_payload_offset_ += rx_size;

            connection_.debug("received: {} bytes ({}/{})", rx_size, current_payload_offset_, header_.payload_length);

            co_return rx_size;
        }

        friend task<size_t> tag_invoke(tag_t<net::async_recv>, rx_message &msg, std::span<std::byte> data) {
            return msg.async_recv(data);
        }

        friend bool tag_invoke(tag_t<net::has_pending_data>, rx_message &msg) {
            return !msg.header_.fin || msg.current_payload_offset_ < msg.header_.payload_length;
        }
    };

    template<bool is_server, typename Socket>
    class tx_message {
        connection<is_server, Socket> &connection_;

        template<bool, typename>
        friend class connection;

        Socket &socket_;
        header header_{
            .fin = false,
            .opcode = op_code::text_frame,
            .mask = not is_server,
            .masking_key = is_server ? details::make_masking_key() : 0,
        };

    public:
        explicit tx_message(connection<is_server, Socket> &conn) noexcept : connection_{conn}, socket_{conn.socket_} {}

        friend task<size_t> tag_invoke(tag_t<net::async_send>, tx_message &msg, std::span<std::byte const> data,
                                       bool fin = false) {
            msg.header_.fin = fin;
            msg.header_.payload_length = data.size();
            co_await msg.header_.send(msg.socket_);
            msg.header_.opcode = op_code::continuation_frame;
            if constexpr (not is_server) {
                std::array<std::byte, 128> masked_data;
                size_t remaining_size = data.size();
                while (remaining_size) {
                    const size_t tmp_size = tl::min(data.size(), masked_data.size(), remaining_size);
                    std::memcpy(masked_data.data(), data.data() + data.size() - remaining_size, tmp_size);
                    msg.header_.mask_body(masked_data);
                    size_t tx_size = co_await net::async_send(msg.socket_, std::span{masked_data.data(), tmp_size});
                    remaining_size -= tx_size;
                    msg.connection_.debug("sent: {} bytes ({}/{})", tx_size, data.size() - remaining_size, data.size());
                }
                co_return data.size();
            } else {
                co_return co_await net::async_send(msg.socket_, data);
            }
        }

        friend task<> tag_invoke(tag_t<net::async_close>, tx_message &msg) {
            if (not msg.header_.fin) {
                msg.header_.fin = true;
                msg.header_.opcode = op_code::continuation_frame;
                msg.header_.payload_length = 0;
                co_await msg.header_.send(msg.socket_);
            }
        }
    };

    template<bool is_server, typename Socket>
    class connection : private logged<"g6::ws::connection"> {
        template<bool, typename>
        friend class rx_message;

        template<bool, typename>
        friend class tx_message;

        Socket socket_;
        net::ip_endpoint remote_endpoint_;
        const uint32_t ws_version;

        status_code status_ = status_code::undefined;
        bool close_sent_ = false;

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
        friend task<rx_message<is_server_, Socket_>> tag_invoke(tag_t<net::async_recv>,
                                                                connection<is_server_, Socket_> &conn, std::stop_token);

        friend task<size_t> tag_invoke(tag_t<net::async_send>, connection &conn, std::span<std::byte const> data) {
            tx_message<is_server, Socket> msg{conn};
            co_return co_await net::async_send(msg, data, true);
        }

        template<typename Job>
        friend task<> tag_invoke(tag_t<net::async_send>, connection &conn, Job &&job) requires
            requires(tx_message<is_server, Socket> &msg) {
            { job(msg) } -> std::same_as<task<>>;
        }
        {
            tx_message<is_server, Socket> msg{conn};
            co_await job(msg);
            co_await net::async_close(msg);
        }

        friend task<> tag_invoke(tag_t<net::async_close>, connection &self,
                                 status_code reason = status_code::normal_closure) {
            if (not self.close_sent_) {
                uint16_t status = uint16_t(reason);
                header h{
                    .fin = true,
                    .opcode = op_code::connection_close,
                    .mask = not is_server,
                    .payload_length = sizeof(status),
                };
                if constexpr (not is_server) { h.masking_key = details::make_masking_key(); }
                co_await h.send(self.socket_);
                // status
                status = byteswap(status);
                h.mask_body(as_writable_bytes(std::span{&status, 1}));
                co_await net::async_send(self.socket_, std::span{&status, 1});
                if (self.status_ == status_code::undefined) {
                    self.status_ = reason;
                }
                self.close_sent_ = true;
            }
        }

        task<rx_message<is_server, Socket>> await() { co_return rx_message<is_server, Socket>{*this}; }

        friend bool tag_invoke(tag_t<net::has_pending_data>, connection &self) {
            return self.status_ == status_code::undefined;
        }

    protected:
        explicit connection(Socket &&socket, net::ip_endpoint const &remote_endpoint,
                            uint32_t version = max_ws_version_) noexcept
            : socket_{std::forward<Socket>(socket)}, remote_endpoint_{remote_endpoint}, ws_version{version} {}
    };

    template<bool is_server, typename Socket>
    task<rx_message<is_server, Socket>> tag_invoke(tag_t<net::async_recv>, connection<is_server, Socket> &conn,
                                                   std::stop_token stop) {
        co_return co_await conn.await();
    }
}// namespace g6::ws
