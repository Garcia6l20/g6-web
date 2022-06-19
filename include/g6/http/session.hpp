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

    public:
        server_session(Socket socket, net::ip_endpoint endpoint) noexcept
            : socket{std::move(socket)}, endpoint_{std::move(endpoint)} {}

        server_session(server_session &&other) noexcept
            : socket{std::move(other.socket)}, endpoint_{std::move(other.endpoint_)} {}

        server_session(server_session const &) = delete;

        friend auto &tag_invoke(tag_t<web::get_socket>, server_session &session) noexcept { return session.socket; }

        friend server_request<Socket> tag_invoke<>(tag_t<net::async_recv>, server_session<Socket> &session);

        template<size_t extent>
        friend task<size_t> tag_invoke(tag_t<net::async_send> const &tag_t, server_session &session,
                                       std::span<std::byte const, extent> data, http::status status = http::status::ok,
                                       http::headers headers = {}) {
            detail::basic_response resp{session.socket, data, status, std::move(headers)};
            co_return co_await net::async_send(resp);
        }

        friend task<detail::chunked_response<Socket>> tag_invoke(tag_t<web::async_message>, server_session &session,
                                                                 http::status status = http::status::ok,
                                                                 http::headers headers = {}) {
            detail::chunked_response<Socket> resp{session.socket, status, std::move(headers)};
            co_await net::async_send(resp);// send http header
            co_return std::move(resp);
        }

        template<typename Job>
        friend task<> tag_invoke(tag_t<web::async_message>, server_session &session, http::status status,
                                 http::headers headers, Job &&job) {
            detail::chunked_response<Socket> resp{session.socket, status, std::move(headers)};
            co_await net::async_send(resp);// send http header
            co_await job(resp);
            co_return co_await net::async_close(resp);
        }

        template<typename Job>
        friend auto tag_invoke(tag_t<web::async_message>, server_session &session, http::status status, Job &&job) {
            return web::async_message(session, status, http::headers{}, std::forward<Job>(job));
        }
    };

    template<typename Socket>
    server_request<Socket> tag_invoke(tag_t<net::async_recv>, server_session<Socket> &session) {
        return {session.socket};
    }

}// namespace g6::http
