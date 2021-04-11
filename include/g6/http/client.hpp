#pragma once

#include <g6/http/http.hpp>
#include <g6/http/impl/static_parser_handler.hpp>

#include <g6/net/ip_endpoint.hpp>
#include <g6/net/net_cpo.hpp>
#include <g6/net/tcp.hpp>
#include <g6/web/proto.hpp>

#include <unifex/any_sender_of.hpp>
#include <unifex/transform.hpp>
#include <unifex/sequence.hpp>
#include <unifex/just.hpp>

namespace g6::io {
template <typename Context2>
auto tag_invoke(unifex::tag_t<net::async_connect>, Context2 &context,
                const g6::web::proto::http_ &,
                const net::ip_endpoint &endpoint);
}

namespace g6::http {

template <typename Context, typename Socket> class client {
  Context &context_;
  Socket socket_;
  using client_buffer = std::array<char, 1024>;
  client_buffer buffer_;
  std::string header_data_;

  struct response : detail::static_parser_handler<false> {
    Socket &socket_;
    client_buffer &buffer_;
    response(Socket &socket, client_buffer &buffer) noexcept
        : socket_{socket}, buffer_{buffer} {}

    response(response &&other) noexcept
        : socket_{other.socket_}, buffer_{other.buffer_} {};

    response(response const &other) = delete;

    friend auto tag_invoke(unifex::tag_t<net::async_recv>, response &response)
        -> unifex::any_sender_of<
            unifex::span<std::byte, unifex::dynamic_extent>> {
      using namespace unifex;
      if (response.has_body()) {
        return just(response.body());
      } else {
        return net::async_recv(response.socket_,
                               as_writable_bytes(span{response.buffer_})) |
               transform([&](size_t bytes) {
                 response.parse(span{response.buffer_.data(), bytes});
                 return response.body();
               });
      }
    }
  };
  response response_;

  client(Context &context, Socket &&socket)
      : context_{context}, socket_{std::forward<Socket>(socket)},
        response_{socket_, buffer_} {}

  template <typename Context2>
  friend auto g6::io::tag_invoke(unifex::tag_t<net::async_connect>,
                                 Context2 &context,
                                 const g6::web::proto::http_ &,
                                 const net::ip_endpoint &endpoint);

  void build_header(std::string_view path, http::method method,
                    http::headers &&headers) noexcept {
    header_data_ = fmt::format(
        "{} {} HTTP/1.1\r\n"
        "UserAgent: cppcoro-http/0.0\r\n",
        detail::http_method_str(static_cast<detail::http_method>(method)),
        path);

    for (auto &[field, value] : headers) {
      header_data_ += fmt::format("{}: {}\r\n", field, value);
    }
    header_data_ += "\r\n";
  }

  friend auto tag_invoke(unifex::tag_t<net::async_send>, client &client,
                         std::string_view path, http::method method,
                         std::string_view data) {
    client.build_header(
        path, method,
        http::headers{{"Content-Length", std::to_string(data.size())}});
    return sequence(net::async_send(client.socket_,
                                    unifex::as_bytes(unifex::span{
                                        client.header_data_.data(),
                                        client.header_data_.size()})) |
                        transform([](auto...) {}),
                    net::async_send(client.socket_,
                                    unifex::as_bytes(unifex::span{data.data(), data.size()})) |
                        transform([](auto...) {})) |
           transform([&]() -> response & { return client.response_; });
    //    return net::async_send(client.socket_,
    //                           as_bytes(span{client.header_data_.data(),
    //                                         client.header_data_.size()})) |
    //           transform([data, &client](size_t) {
    //             return net::async_send(client.socket_,
    //                                    as_bytes(span{data.data(),
    //                                    data.size()}));
    //           }) |
    //           transform([&](size_t sent) {
    //             return net::async_recv(client.socket_,
    //                                    as_writable_bytes(span{client.buffer_}))
    //                                    |
    //                    transform([&](size_t bytes) {
    //                      client.response_.parse(
    //                          span{client.buffer_.data(), bytes});
    //                      return just(client.response_);
    //                    });
    //           });
  }

public:
  client(client &&other) noexcept
      : context_{other.context_}, socket_{std::move(other.socket_)},
        response_{socket_, buffer_} {}
  client(client const &other) = delete;
};
} // namespace g6::http

namespace g6::io {
template <typename Context>
auto tag_invoke(unifex::tag_t<net::async_connect>, Context &context,
                const g6::web::proto::http_ &,
                const net::ip_endpoint &endpoint) {
  return unifex::transform(net::open_socket(context, net::tcp_client, endpoint),
                           [&](auto sock) {
                             return g6::http::client{context, std::move(sock)};
                           });
}
} // namespace g6::io
