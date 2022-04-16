#pragma once

#include <g6/http/server.hpp>
#include <g6/web/context.hpp>
#include <g6/ws/server.hpp>

#include <g6/net/async_socket.hpp>

namespace g6::web {

    inline auto tag_invoke(tag_t<g6::web::make_server>, g6::web::context &ctx, web::proto::http_ const &,
                           net::ip_endpoint endpoint) {
        auto socket = net::open_socket(ctx, net::proto::tcp);
        socket.bind(endpoint);
        socket.listen();
        return http::server{ctx, std::move(socket)};
    }

    inline auto tag_invoke(tag_t<g6::web::make_server>, g6::web::context &ctx, web::proto::https_ const &,
                           net::ip_endpoint endpoint, const auto &cert, const auto &key) {
        auto socket = net::open_socket(ctx, std::move(endpoint), cert, key);
        return http::server{ctx, std::move(socket)};
    }

    inline auto tag_invoke(tag_t<g6::web::make_server>, g6::web::context &ctx, web::proto::ws_ const &,
                           net::ip_endpoint endpoint) {
        auto socket = net::open_socket(ctx, net::proto::tcp);
        socket.bind(endpoint);
        socket.listen();
        return ws::server{ctx, std::move(socket)};
    }

    inline auto tag_invoke(tag_t<g6::web::make_server>, g6::web::context &ctx, web::proto::wss_ const &,
                           net::ip_endpoint endpoint, const auto &cert, const auto &key) {
        auto socket = net::open_socket(ctx, std::move(endpoint), cert, key);
        return ws::server{ctx, std::move(socket)};
    }
}// namespace g6::web
