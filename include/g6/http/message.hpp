/**
 * @file g6/http/message.hpp
 * @author Garcia Sylvain <garcia.6l20@gmail.com>
 */
#pragma once

#include <g6/http/parser.hpp>
#include <g6/net/net_cpo.hpp>
#include <g6/coro/cpo/file.hpp>

#include <fmt/format.h>

#include <span>
#include <stop_token>

namespace g6::http { namespace detail {

    template<typename Socket, bool is_request>
    struct parser_base;

    template<typename Socket, bool is_request>
    inline bool tag_invoke(tag_t<g6::has_pending_data>,
                           g6::http::detail::parser_base<Socket, is_request> &) noexcept;

    template<typename Socket, bool is_request>
    struct parser_base : parser<is_request> {

        Socket &socket_;

        parser_base(Socket &sock) noexcept : socket_{sock} {}
        parser_base(parser_base const &) = delete;
        parser_base(parser_base &&other) noexcept : parser<is_request>{std::move(other)}, socket_{other.socket_} {}

        template<size_t extent>
        friend task<size_t> tag_invoke(tag_t<net::async_recv>, parser_base &self, std::span<std::byte, extent> data) {
            bool done = false;
            size_t body_size = 0;
            do {
                size_t bytes_received = co_await net::async_recv(self.socket_, data);
                if (bytes_received == 0) {//
                    throw std::system_error{std::make_error_code(std::errc::connection_reset)};
                }
                done = self(as_bytes(std::span{data.data(), bytes_received}),
                            [data, &body_size](std::span<std::byte const> in) {
                                // move body to begining of data
                                std::memmove(data.data() + body_size, in.data(), in.size());
                                body_size += in.size();
                            });
            } while ((body_size == 0) and (not done));
            co_return body_size;
        }

        friend bool tag_invoke(tag_t<g6::has_pending_data>,
                               g6::http::detail::parser_base<Socket, is_request> &self) noexcept {
            return not bool(self);
        }
    };

    template<typename Socket>
    using request_parser = detail::parser_base<Socket, true>;

    template<typename Socket>
    using response_parser = detail::parser_base<Socket, false>;

    template<typename Super>
    struct message_builder_base {

    private:
        auto &super() { return *static_cast<Super *>(this); }
        auto const &super() const { return *static_cast<Super const *>(this); }

        inline std::string _header_base() const {
            if constexpr (Super::is_request) {
                return format("{} {} HTTP/1.1\r\n"
                              "UserAgent: g6-http/0.0\r\n",
                              to_string(super().method), super().path);
            } else {
                return format("HTTP/1.1 {} {}\r\n"
                              "UserAgent: g6-http/0.0\r\n",
                              int(super().status), to_string(super().status));
            }
        }

    protected:
        bool header_sent_ = false;

        task<size_t> send_header() {
            std::string header = _header_base();
            for (const auto &[field, value] : super().headers) {
                format_to(std::back_inserter(header), "{}: {}\r\n", field, value);
            }
            header += "\r\n";
            header_sent_ = true;
            co_return co_await net::async_send(super().socket, header);
        }
    };

    template<bool is_request_, typename Socket>
    struct message_base;

    template<typename Socket>
    struct message_base<true, Socket> : message_builder_base<message_base<true, Socket>> {
        static constexpr bool is_request = true;
        Socket &socket;
        http::method method;
        std::string path;
        http::headers headers;
        message_base() = delete;
        message_base(message_base const &) = delete;
        message_base(message_base &&) = default;
        message_base(Socket &socket_, http::method method_, std::string_view path_, http::headers headers_) noexcept
            : socket{socket_}, method{method_}, path{path_}, headers{std::move(headers_)} {}
    };

    template<typename Socket>
    struct message_base<false, Socket> : message_builder_base<message_base<false, Socket>> {
        static constexpr bool is_request = false;
        Socket &socket;
        http::status status;
        http::headers headers;
        message_base() = delete;
        message_base(message_base const &) = delete;
        message_base(message_base &&) = default;
        message_base(Socket &socket_, http::status status_, http::headers headers_) noexcept
            : socket{socket_}, status{status_}, headers{std::move(headers_)} {}
    };

    template<bool is_request, typename Socket, size_t extent>
    struct basic_message : message_base<is_request, Socket> {
        std::span<std::byte const, extent> data;

        template<typename... Args>
        basic_message(Socket &socket, std::span<std::byte const, extent> buffer, Args &&...args)
            : message_base<is_request, Socket>{socket, std::forward<Args>(args)...}, data{buffer} {
            this->headers.emplace("Content-Length", std::to_string(data.size()));
        }

        // send data
        friend task<size_t> tag_invoke(tag_t<net::async_send>, basic_message &self) {
            co_await self.send_header();
            co_return co_await net::async_send(self.socket, self.data);
        }
    };

    template<typename Socket, size_t extent>
    using basic_request = basic_message<true, Socket, extent>;

    template<typename Socket, size_t extent>
    using basic_response = basic_message<false, Socket, extent>;

    template<bool is_request, typename Socket>
    struct chunked_message : message_base<is_request, Socket> {
        bool closed = false;

        template<typename... Args>
        chunked_message(Socket &socket, Args &&...args)
            : message_base<is_request, Socket>{socket, std::forward<Args>(args)...} {
            this->headers.emplace("Transfer-Encoding", "chunked");
        }
        chunked_message(chunked_message const &) = delete;
        chunked_message(chunked_message &&other) noexcept
            : message_base<is_request, Socket>{std::move(other)}, closed{std::exchange(other.closed, true)} {}

        chunked_message &operator=(chunked_message const &) = delete;
        chunked_message &operator=(chunked_message &&other) noexcept { closed = std::exchange(other.closed, true); }

        // send header
        friend task<void> tag_invoke(tag_t<net::async_send>, chunked_message &self) {
            if (!self.header_sent_) { co_await self.send_header(); }
        }

        // send data
        template<size_t extent>
        friend task<size_t> tag_invoke(tag_t<net::async_send>, chunked_message &self,
                                       std::span<std::byte const, extent> buffer) {
            if (!self.header_sent_) { co_await self.send_header(); }
            co_await net::async_send(self.socket, format("{:x}\r\n", buffer.size()));
            size_t sent_bytes = co_await net::async_send(self.socket, buffer);
            co_await net::async_send(self.socket, std::string_view{"\r\n"});
            co_return sent_bytes;
        }

        /**
             * @brief Close a request chunked message
             * @returns The response parser
             */
        friend task<response_parser<Socket>> tag_invoke(tag_t<async_close>,
                                                        chunked_message &self) requires(is_request) {
            co_await net::async_send(self.socket, std::string_view{"0\r\n\r\n"});
            self.closed = true;
            co_return response_parser<Socket>{self.socket};
        }

        /**
             * @brief Close a response chunked message
             */
        friend task<void> tag_invoke(tag_t<async_close>, chunked_message &self) requires(!is_request) {
            co_await net::async_send(self.socket, std::string_view{"0\r\n\r\n"});
            self.closed = true;
        }
    };

    template<typename Socket>
    using chunked_request = chunked_message<true, Socket>;

    template<typename Socket>
    using chunked_response = chunked_message<false, Socket>;

}}// namespace g6::http::detail
