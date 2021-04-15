#pragma once

#include <g6/http/http.hpp>
#include <g6/http/impl/static_parser_handler.hpp>

#include <g6/net/ip_endpoint.hpp>
#include <g6/net/net_cpo.hpp>

#include <g6/ssl/async_socket.hpp>

#include <g6/web/proto.hpp>

#include <g6/web/web_cpo.hpp>
#include <unifex/any_sender_of.hpp>
#include <unifex/just.hpp>
#include <unifex/let_with.hpp>
#include <unifex/sequence.hpp>
#include <unifex/transform.hpp>

namespace g6::io {
    template<typename Context2>
    auto tag_invoke(unifex::tag_t<net::async_connect>, Context2 &context, const g6::web::proto::http_ &,
                    const net::ip_endpoint &endpoint);
    template<typename Context2>
    auto tag_invoke(unifex::tag_t<net::async_connect>, Context2 &context, const g6::web::proto::https_ &,
                    const net::ip_endpoint &endpoint, ssl::verify_flags verify_flags = ssl::verify_flags::none);
}// namespace g6::io

namespace g6::http {

    template<typename Context, typename Socket>
    class client
    {
        using client_buffer = std::array<std::byte, 1024>;

    public:
        Socket socket;

        struct response : detail::static_parser_handler<false> {
            Socket &socket_;
            span<std::byte> buffer_;
            response(Socket &socket, span<std::byte> &buffer) noexcept : socket_{socket}, buffer_{buffer} {
            }

            response(response &&other) noexcept : socket_{other.socket_}, buffer_{other.buffer_} {};

            response(response const &other) = delete;

            friend task<unifex::span<std::byte>> tag_invoke(unifex::tag_t<net::async_recv>, response &response) {
                using namespace unifex;
                if (response.has_body()) {
                    co_return response.body();
                } else {
                    size_t bytes = co_await net::async_recv(response.socket_, as_writable_bytes(response.buffer_));
                    response.parse(as_bytes(span{response.buffer_.data(), bytes}));
                    co_return response.body();
                }
            }
        };

    protected:
        Context &context_;
        client_buffer buffer_data_{};
        span<std::byte> buffer_{buffer_data_.data(), buffer_data_.size()};
        std::string header_data_;


        client(Context &context, Socket &&socket) : context_{context}, socket{std::forward<Socket>(socket)} {}

        friend auto& tag_invoke(unifex::tag_t<web::get_context>, client &client) {
            return client.context_;
        }
        friend auto& tag_invoke(unifex::tag_t<web::get_socket>, client &client) {
            return client.socket;
        }

        template<typename Context2>
        friend auto g6::io::tag_invoke(unifex::tag_t<net::async_connect>, Context2 &context,
                                       const g6::web::proto::http_ &, const net::ip_endpoint &endpoint);
        template<typename Context2>
        friend auto g6::io::tag_invoke(unifex::tag_t<net::async_connect>, Context2 &context,
                                       const g6::web::proto::https_ &, const net::ip_endpoint &endpoint,
                                       ssl::verify_flags);

        void build_header(std::string_view path, http::method method, http::headers &&headers) noexcept {
            header_data_ = fmt::format("{} {} HTTP/1.1\r\n"
                                       "UserAgent: cppcoro-http/0.0\r\n",
                                       detail::http_method_str(static_cast<detail::http_method>(method)), path);

            for (auto &[field, value] : headers) { header_data_ += fmt::format("{}: {}\r\n", field, value); }
            header_data_ += "\r\n";
        }

        friend task<response> tag_invoke(unifex::tag_t<net::async_send>, client &client, std::string_view path,
                               http::method method, span<std::byte const> data, http::headers hdrs) {
            if (data.size()) { hdrs.template emplace("Content-Length", std::to_string(data.size())); }
            client.build_header(path, method, std::move(hdrs));
            co_await net::async_send(client.socket, unifex::as_bytes(unifex::span{client.header_data_.data(),
                                                                                             client.header_data_.size()}));
            co_await net::async_send(client.socket, data);
            co_return response{client.socket, client.buffer_};
        }

        friend auto tag_invoke(unifex::tag_t<net::async_send>, client &client, std::string_view path,
                               http::method method) {
            static constexpr span empty{static_cast<std::byte const *>(nullptr), 0};
            return net::async_send(client, path, method, empty, http::headers{});
        }

        friend auto tag_invoke(unifex::tag_t<net::async_send>, client &client, std::string_view path,
                               http::method method, http::headers &&hdrs) {
            static constexpr span empty{static_cast<std::byte const *>(nullptr), 0};
            return net::async_send(client, path, method, empty, std::forward<http::headers>(hdrs));
        }

        friend auto tag_invoke(unifex::tag_t<net::async_send>, client &client, std::string_view path,
                               http::method method, span<std::byte const> data) {
            return net::async_send(client, path, method, data, http::headers{});
        }

    public:
        client(client &&other) noexcept : context_{other.context_}, socket{std::move(other.socket)} {}
        client(client const &other) = delete;
    };
}// namespace g6::http

namespace g6::io {

    template<typename Context>
    auto tag_invoke(unifex::tag_t<net::async_connect>, Context &context, const g6::web::proto::http_ &,
                    const net::ip_endpoint &endpoint) {
        return let_with([&] { return net::open_socket(context, net::tcp_client); },
                        [&](auto &sock) {
                            return net::async_connect(sock, endpoint) | transform([&](int) { return g6::http::client{context, std::move(sock)}; });
                        });
    }

    template<typename Context>
    auto tag_invoke(unifex::tag_t<net::async_connect>, Context &context, const g6::web::proto::https_ &,
                    const net::ip_endpoint &endpoint, ssl::verify_flags verify_flags) {
        return let_with([&context] { return net::open_socket(context, ssl::tcp_client); },
                        [&context, &endpoint, verify_flags](auto &sock) {
                            sock.host_name("localhost");
                            sock.set_peer_verify_mode(ssl::peer_verify_mode::required);
                            sock.set_verify_flags(verify_flags);
                            return net::async_connect(sock, endpoint) | transform([&context, &sock] {
                                       return g6::http::client{context, std::move(sock)};
                                   });
                        });
    }
}// namespace g6::io
