#pragma once

#include <g6/net/socket_protocols.hpp>

namespace g6::web::proto {
    inline constexpr struct http_ : net::proto::tcp_t {
    } http;
    inline constexpr struct https_ : net::proto::tcp_t {
    } https;
    inline constexpr struct ws_ : net::proto::tcp_t {
    } ws;
    inline constexpr struct wss_ : net::proto::tcp_t {
    } wss;
}// namespace g6::web::proto
