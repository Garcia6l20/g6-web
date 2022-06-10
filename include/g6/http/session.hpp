#pragma once

#include <g6/http/impl/static_parser_handler.hpp>

#include <g6/net/ip_endpoint.hpp>
#include <g6/net/net_cpo.hpp>
#include <g6/web/web_cpo.hpp>

#include <g6/coro/task.hpp>

#include <algorithm>
#include <stop_token>
#include <string_view>

namespace g6::http {

    template<typename Socket>
    struct [[nodiscard]] server_response;

    template<typename T, size_t extent, typename Socket_>
    task<size_t> tag_invoke(tag_t<g6::net::async_send>, g6::http::server_response<Socket_> &stream,
                            std::span<T, extent> data);


    template<typename Socket>
    struct [[nodiscard]] server_response {
        Socket &socket_;
        bool closed_{false};
        std::string size_str;

        server_response(Socket &sock) noexcept : socket_{sock} {}

        server_response(server_response &&other) noexcept
            : socket_{other.socket_}, closed_{std::exchange(other.closed_, true)}, size_str{std::move(other.size_str)} {
        }
        server_response(server_response const &) = delete;

        ~server_response() noexcept { assert(closed_); }

        template<typename T, size_t extent, typename Socket_>
        friend task<size_t> tag_invoke(tag_t<g6::net::async_send>, g6::http::server_response<Socket_> &stream,
                                       std::span<T, extent> data) {
            assert(!stream.closed_);
            stream.size_str = fmt::format("{:x}\r\n", data.size());
            co_await net::async_send(stream.socket_, stream.size_str);
            co_await net::async_send(stream.socket_, data);
            co_await net::async_send(stream.socket_, "\r\n");
            co_return data.size();
        }

        friend auto tag_invoke(tag_t<net::async_send>, server_response &stream) {
            stream.closed_ = true;
            return net::async_send(stream.socket_, "0\r\n\r\n");
        }
    };

    using session_buffer = std::array<char, 1024>;

    template<typename Socket>
    struct server_request : detail::static_parser_handler<true> {
        Socket &socket_;
        session_buffer &buffer_;
        server_request(Socket &socket, session_buffer &buffer) noexcept : socket_{socket}, buffer_{buffer} {}

        server_request(server_request &&other) noexcept
            : detail::static_parser_handler<true>{std::forward<server_request>(other)}, socket_{other.socket_},
              buffer_{other.buffer_} {};

        server_request(server_request const &other) = delete;

        friend task<std::span<std::byte const>> tag_invoke(tag_t<net::async_recv>, server_request &request) {
            if (request.has_body()) {
                co_return request.body();
            } else {
                size_t bytes = co_await net::async_recv(request.socket_, as_writable_bytes(std::span{request.buffer_}));
                if (bytes == 0) { throw std::system_error{std::make_error_code(std::errc::connection_reset)}; }
                request.parse(as_bytes(std::span{request.buffer_.data(), bytes}));
                co_return request.body();
            }
        }
    };

    template<typename Socket>
    class server_session {
    public:
        Socket socket;

        auto const &remote_endpoint() const noexcept { return endpoint_; }

    protected:
        net::ip_endpoint endpoint_;
        session_buffer buffer_;
        std::string header_data_;

        void build_header(http::status status, http::headers &&headers) noexcept {
            header_data_ = fmt::format("HTTP/1.1 {} {}\r\n"
                                       "UserAgent: g6-http/0.0\r\n",
                                       int(static_cast<detail::http_status>(status)),
                                       detail::http_status_str(static_cast<detail::http_status>(status)));

            for (auto &[field, value] : headers) { header_data_ += fmt::format("{}: {}\r\n", field, value); }
            header_data_ += "\r\n";
        }

    public:
        server_session(Socket socket, net::ip_endpoint endpoint) noexcept
            : socket{std::move(socket)}, endpoint_{std::move(endpoint)} {}

        server_session(server_session &&other) noexcept
            : socket{std::move(other.socket)}, endpoint_{std::move(other.endpoint_)}, buffer_{std::move(other.buffer_)},
              header_data_{std::move(other.header_data_)} {}

        server_session(server_session const &) = delete;

        friend auto &tag_invoke(tag_t<web::get_socket>, server_session &session) noexcept { return session.socket; }

        friend task<server_request<Socket>> tag_invoke(tag_t<net::async_recv>, server_session &session,
                                                       std::stop_token stop = {}) {
            size_t bytes = co_await net::async_recv(
                session.socket, as_writable_bytes(std::span{session.buffer_.data(), session.buffer_.size()}), stop);
            if (bytes == 0) { throw std::system_error{std::make_error_code(std::errc::connection_reset)}; }
            server_request req{session.socket, session.buffer_};
            req.parse(as_bytes(std::span{session.buffer_.data(), bytes}));
            co_return req;
        }

        template<typename T, size_t extent>
        friend task<size_t> tag_invoke(tag_t<net::async_send>, server_session &session, http::status status,
                                       http::headers hdrs, std::span<T, extent> data) {
            if (data.size()) { hdrs.emplace("Content-Length", std::to_string(data.size())); }
            session.build_header(status, std::move(hdrs));
            co_await net::async_send(session.socket, session.header_data_);
            co_return co_await net::async_send(session.socket, data);
        }

        template<typename T, size_t extent>
        friend auto tag_invoke(tag_t<net::async_send> const &tag_t, server_session &session, http::status status,
                               std::span<T, extent> data) {
            return net::async_send(session, status, http::headers{}, data);
        }

        friend auto tag_invoke(tag_t<net::async_send> const &tag_t, server_session &session, http::status status) {
            return net::async_send(session, status, http::headers{},
                                   std::span{static_cast<const std::byte *>(nullptr), 0});
        }

        friend task<server_response<Socket>> tag_invoke(tag_t<net::async_send>, server_session &session,
                                                        http::status status, http::headers &&headers) {
            session.build_header(status, std::forward<http::headers>(headers));
            size_t sz = co_await net::async_send(session.socket, session.header_data_);
            co_return server_response{session.socket};
        }
    };

}// namespace g6::http
