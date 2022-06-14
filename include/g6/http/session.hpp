#pragma once

#include <g6/http/message.hpp>

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
    class server_session;

    template<typename Socket>
    class server_request;

    template<typename Socket>
    server_request<Socket> tag_invoke(tag_t<net::async_recv>, server_session<Socket> &session);

    template<typename Socket>
    class server_request : public detail::request_parser<Socket> {
        friend server_request<Socket> tag_invoke<>(tag_t<net::async_recv>, server_session<Socket> &session);
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

        friend server_request<Socket> tag_invoke<>(tag_t<net::async_recv>, server_session<Socket> &session);

        template<size_t extent>
        friend task<size_t> tag_invoke(tag_t<net::async_send> const &tag_t, server_session &session,
                               std::span<std::byte const, extent> data, http::status status = http::status::ok, http::headers headers = {}) {
            if (data.size()) { headers.emplace("Content-Length", std::to_string(data.size())); }
            session.build_header(status, std::move(headers));
            co_await net::async_send(session.socket, session.header_data_);
            co_return co_await net::async_send(session.socket, data);
        }

        friend task<server_response<Socket>> tag_invoke(tag_t<net::async_send>, server_session &session,
                                                        http::status status, http::headers &&headers) {
            session.build_header(status, std::forward<http::headers>(headers));
            size_t sz = co_await net::async_send(session.socket, session.header_data_);
            co_return server_response{session.socket};
        }
    };

    template<typename Socket>
    server_request<Socket> tag_invoke(tag_t<net::async_recv>, server_session<Socket> &session) {
        return {session.socket};
    }

}// namespace g6::http
