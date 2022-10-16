#pragma once

#include <g6/http/http.hpp>
#include <g6/http/message.hpp>

#include <g6/net/ip_endpoint.hpp>
#include <g6/net/net_cpo.hpp>

#include <g6/ssl/async_socket.hpp>

#include <g6/web/proto.hpp>
#include <g6/web/web_cpo.hpp>

namespace g6::http {

    template<typename Context, typename Socket>
    class client;

    template<typename Socket>
    struct client_response;

    template<typename Context, typename Socket>
    client_response<Socket> tag_invoke(tag_t<g6::net::async_recv>, client<Context, Socket> &);

    template<typename Socket>
    struct client_response : detail::response_parser<Socket> {
        template<typename Context>
        friend client_response<Socket> tag_invoke(tag_t<g6::net::async_recv>, client<Context, Socket> &);
    };

    template<typename Context, typename Socket>
    class client {
    public:
        Socket socket;


        auto const &remote_endpoint() const noexcept { return remote_endpoint_; }

        client(Context &context, Socket &&socket, net::ip_endpoint const &remote_endpoint)
            : context_{context}, socket{std::forward<Socket>(socket)}, remote_endpoint_{remote_endpoint} {}

    protected:
        Context &context_;
        net::ip_endpoint remote_endpoint_;

        friend auto &tag_invoke(tag_t<web::get_context>, client &client) { return client.context_; }
        friend auto &tag_invoke(tag_t<web::get_socket>, client &client) { return client.socket; }

        friend client_response<Socket> tag_invoke<>(tag_t<g6::net::async_recv>, client<Context, Socket> &self);

        friend task<client_response<Socket>> tag_invoke(tag_t<net::async_send>, client &client,
                                                        std::span<std::byte const> data, std::string_view path,
                                                        http::method method = http::method::get, http::headers hdrs = {}) {
            detail::basic_request req{client.socket, data, method, path, std::move(hdrs)};
            co_await net::async_send(req);
            co_return net::async_recv(client);
        }

        template <typename Job>
        friend task<detail::response_parser<Socket>> tag_invoke(tag_t<net::async_send>, client &client, std::string_view path,
                                                        http::method method, http::headers hdrs, Job &&job)
        requires requires(detail::chunked_request<Socket> &req) {
            {job(req)} -> std::same_as<task<>>;
        } {
            detail::chunked_request<Socket> req{client.socket, method, path, std::move(hdrs)};
            co_await net::async_send(req); // send http header
            co_await job(req);
            co_return co_await net::async_close(req);
        }

        template <typename Job>
        friend auto tag_invoke(tag_t<net::async_send>, client &client, std::string_view path,
                                                        http::method method, Job &&job)
        requires requires(detail::chunked_request<Socket> &req) {
            {job(req)} -> std::same_as<task<>>;
        } {
            return net::async_send(client, path, method, http::headers{}, std::forward<Job>(job));
        }

        template <typename Job>
        friend auto tag_invoke(tag_t<net::async_send>, client &client, std::string_view path,
                                                        Job &&job)
        requires requires(detail::chunked_request<Socket> &req) {
            {job(req)} -> std::same_as<task<>>;
        } {
            return net::async_send(client, path, http::method::get, http::headers{}, std::forward<Job>(job));
        }

    public:
        client(client &&other) noexcept
            : context_{other.context_}, socket{std::move(other.socket)}, remote_endpoint_{
                                                                             std::move(other.remote_endpoint_)} {}
        client(client const &other) = delete;
    };

    template<typename Context, typename Socket>
    client_response<Socket> tag_invoke(tag_t<g6::net::async_recv>, client<Context, Socket> &self) {
        return {self.socket};
    }

}// namespace g6::http

namespace g6::net {

    template<typename Context>
    task<http::client<Context, net::async_socket>> tag_invoke(tag_t<net::async_connect>, Context &context,
                                                              const g6::web::proto::http_ &,
                                                              const net::ip_endpoint &endpoint) {
        auto sock = net::open_socket(context, net::proto::tcp);
        sock.bind(net::ipv4_endpoint{});// TODO handle this for windows
        co_await net::async_connect(sock, endpoint);
        co_return g6::http::client{context, std::move(sock), endpoint};
    }

    template<typename Context>
    task<http::client<Context, ssl::async_socket>>
    tag_invoke(tag_t<net::async_connect>, Context &context, const g6::web::proto::https_ &,
               const net::ip_endpoint &endpoint, ssl::verify_flags verify_flags = ssl::verify_flags::none) {
        auto sock = net::open_socket(context, proto::secure_tcp);
        sock.bind(net::ipv4_endpoint{});// TODO handle this for windows
        sock.host_name("localhost");
        sock.set_peer_verify_mode(ssl::peer_verify_mode::required);
        sock.set_verify_flags(verify_flags);
        co_await net::async_connect(sock, endpoint);
        co_return g6::http::client{context, std::move(sock), endpoint};
    }
}// namespace g6::net
