#pragma once

#include <g6/http/http.hpp>
#include <g6/http/impl/static_parser_handler.hpp>

#include <g6/net/ip_endpoint.hpp>
#include <g6/net/net_cpo.hpp>

#include <g6/ssl/async_socket.hpp>

#include <g6/web/proto.hpp>
#include <g6/web/web_cpo.hpp>

namespace g6::http {

    template<typename Context, typename Socket>
    class client {
        using client_buffer = std::array<std::byte, 1024>;

    public:
        Socket socket;

        struct response : detail::static_parser_handler<false> {
            Socket &socket_;
            std::span<std::byte> buffer_;
            response(Socket &socket, std::span<std::byte> &buffer) noexcept : socket_{socket}, buffer_{buffer} {}

            response(response &&other) noexcept : socket_{other.socket_}, buffer_{other.buffer_} {};

            response(response const &other) = delete;

            friend inline task<std::span<std::byte>> tag_invoke(tag_t<g6::net::async_recv>, response &response) {
                if (response.has_body()) {
                    co_return response.body();
                } else {
                    size_t bytes = co_await net::async_recv(response.socket_, as_writable_bytes(response.buffer_));
                    if (bytes == 0) { throw std::system_error{std::make_error_code(std::errc::connection_reset)}; }
                    response.parse(as_bytes(std::span{response.buffer_.data(), bytes}));
                    co_return response.body();
                }
            }
        };

        auto const &remote_endpoint() const noexcept { return remote_endpoint_; }

        client(Context &context, Socket &&socket, net::ip_endpoint const &remote_endpoint)
            : context_{context}, socket{std::forward<Socket>(socket)}, remote_endpoint_{remote_endpoint} {}

    protected:
        Context &context_;
        net::ip_endpoint remote_endpoint_;
        client_buffer buffer_data_{};
        std::span<std::byte> buffer_{buffer_data_.data(), buffer_data_.size()};
        std::string header_data_;

        friend auto &tag_invoke(tag_t<web::get_context>, client &client) { return client.context_; }
        friend auto &tag_invoke(tag_t<web::get_socket>, client &client) { return client.socket; }

        void build_header(std::string_view path, http::method method, http::headers &&headers) noexcept {
            header_data_ = fmt::format("{} {} HTTP/1.1\r\n"
                                       "UserAgent: g6-http/0.0\r\n",
                                       detail::http_method_str(static_cast<detail::http_method>(method)), path);

            for (auto &[field, value] : headers) { header_data_ += fmt::format("{}: {}\r\n", field, value); }
            header_data_ += "\r\n";
        }

        friend task<response> tag_invoke(tag_t<net::async_send>, client &client, std::string_view path,
                                         http::method method, std::span<std::byte const> data, http::headers hdrs) {
            if (data.size()) { hdrs.emplace("Content-Length", std::to_string(data.size())); }
            client.build_header(path, method, std::move(hdrs));
            co_await net::async_send(client.socket,
                                     as_bytes(std::span{client.header_data_.data(), client.header_data_.size()}));
            co_await net::async_send(client.socket, data);
            co_return response{client.socket, client.buffer_};
        }

        friend auto tag_invoke(tag_t<net::async_send>, client &client, std::string_view path, http::method method) {
            static constexpr std::span empty{static_cast<std::byte const *>(nullptr), 0};
            return net::async_send(client, path, method, empty, http::headers{});
        }

        friend auto tag_invoke(tag_t<net::async_send>, client &client, std::string_view path, http::method method,
                               http::headers &&hdrs) {
            static constexpr std::span empty{static_cast<std::byte const *>(nullptr), 0};
            return net::async_send(client, path, method, empty, std::forward<http::headers>(hdrs));
        }

        friend auto tag_invoke(tag_t<net::async_send>, client &client, std::string_view path, http::method method,
                               std::span<std::byte const> data) {
            return net::async_send(client, path, method, data, http::headers{});
        }

    public:
        client(client &&other) noexcept
            : context_{other.context_}, socket{std::move(other.socket)}, remote_endpoint_{
                                                                             std::move(other.remote_endpoint_)} {}
        client(client const &other) = delete;
    };

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
               const net::ip_endpoint &endpoint, ssl::verify_flags verify_flags) {
        auto sock = net::open_socket(context, ssl::tcp_client);
        sock.bind(net::ipv4_endpoint{});// TODO handle this for windows
        sock.host_name("localhost");
        sock.set_peer_verify_mode(ssl::peer_verify_mode::required);
        sock.set_verify_flags(verify_flags);
        co_await net::async_connect(sock, endpoint);
        co_return g6::http::client{context, std::move(sock), endpoint};
    }
}// namespace g6::net
