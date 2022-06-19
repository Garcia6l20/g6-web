#include <catch2/catch.hpp>
#include <spdlog/spdlog.h>

#include <g6/web/context.hpp>

#include <g6/ws/client.hpp>
#include <g6/ws/server.hpp>

#include <g6/coro/sync_wait.hpp>
#include <g6/scope_guard.hpp>

using namespace g6;

// TEST_CASE("ws simple server", "[g6::web::ws]") {
//     spdlog::set_level(spdlog::level::debug);

//     web::context ctx{};
//     std::stop_source stop_source{};

//     auto server = web::make_server(ctx, web::proto::ws, *from_string<net::ip_endpoint>("127.0.0.1:0"));
//     auto server_endpoint = *server.socket.local_endpoint();

//     spdlog::info("server listening at: {}", server_endpoint);

//     sync_wait(
//         [&]() -> task<void> {
//             co_await web::async_serve(server, stop_source.get_token(), [&] {
//                 return [&stop_source]<typename Session>(Session session, std::stop_token stop_token) -> task<void> {
//                     scope_guard _ = [&]() noexcept {//
//                         stop_source.request_stop();
//                     };
//                     auto message = co_await net::async_recv(session);
//                     while (net::has_pending_data(message)) {
//                         std::array<std::byte, 256> data;
//                         auto sz = co_await net::async_recv(message, data);
//                         if (sz) {
//                             auto sv_body = std::string_view{reinterpret_cast<const char *>(data.data()), *sz};
//                             spdlog::info("body: {}", sv_body);
//                             REQUIRE(sv_body == "Hello !");
//                         } else {
//                             spdlog::info("closed: {}", to_string(sz.error()));
//                         }
//                     }
//                     co_await net::async_send(session, as_bytes(std::span{"OK !", 4}));
//                 };
//             });
//         }(),
//         [&]() -> task<void> {
//             auto session = co_await net::async_connect(ctx, web::proto::ws, server_endpoint);
//             co_await net::async_send(session, as_bytes(std::span{"Hello !", 7}));
//             auto message = co_await net::async_recv(session);
//             std::string body_str;
//             while (net::has_pending_data(message)) {
//                 std::array<std::byte, 256> data;
//                 auto sz = co_await net::async_recv(message, data);
//                 auto body_sv = std::string_view{reinterpret_cast<const char *>(data.data()), *sz};
//                 body_str += body_sv;
//             }
//             spdlog::info("body: {}", body_str);
//             REQUIRE(body_str == "OK !");
//         }(),
//         async_exec(ctx, stop_source.get_token()));
// }

#include <random>

std::string make_random_string(size_t size) noexcept {
    static constexpr char alphanum[] = "0123456789"
                                       "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                       "abcdefghijklmnopqrstuvwxyz";
    std::string result;
    result.reserve(size);
    static std::mt19937 rng{size_t(time(nullptr) * getpid())};
    std::ranges::generate_n(std::back_inserter(result), size,
                            [&]() { return alphanum[rng() % (sizeof(alphanum) - 1)]; });
    return result;
}

TEST_CASE("ws simple server: segmented", "[g6::web::ws]") {
    spdlog::set_level(spdlog::level::debug);

    web::context ctx{};
    std::stop_source stop_source{};

    auto server = web::make_server(ctx, web::proto::ws, *from_string<net::ip_endpoint>("127.0.0.1:0"));
    auto server_endpoint = *server.socket.local_endpoint();

    spdlog::info("server listening at: {}", server_endpoint);

    sync_wait(
        [&]() -> task<void> {
            co_await web::async_serve(server, stop_source.get_token(), [&] {
                return [&stop_source]<typename Session>(Session session, std::stop_token stop_token) -> task<void> {
                    scope_guard _ = [&]() noexcept {//
                        stop_source.request_stop();
                    };
                    std::string rx_body;
                    while (net::has_pending_data(session)) {
                        auto message = co_await net::async_recv(session);
                        co_await net::async_recv(message, std::back_inserter(rx_body));
                        if (rx_body.size()) {
                            spdlog::info("body: {}", rx_body);
                            co_await net::async_send(session, as_bytes(std::span{rx_body.data(), rx_body.size()}));
                        }
                        rx_body.clear();
                    }
                    spdlog::info("session closed: {}", to_string(session.status()));
                };
            });
        }(),
        [&]() -> task<void> {
            auto session = co_await net::async_connect(ctx, web::proto::ws, server_endpoint);
            const std::string tx_body = make_random_string(1021);            
            co_await net::async_send(session, tx_body);
            auto response = co_await net::async_recv(session);
            std::string rx_body;
            co_await net::async_recv(response, std::back_inserter(rx_body));
            co_await net::async_close(session);
            spdlog::info("client rx body: {}", rx_body);
            REQUIRE(rx_body == tx_body);
        }(),
        async_exec(ctx, stop_source.get_token()));
}

TEST_CASE("ws simple server: segmented/generator like", "[g6::web::ws]") {
    spdlog::set_level(spdlog::level::debug);

    web::context ctx{};
    std::stop_source stop_source{};

    auto server = web::make_server(ctx, web::proto::ws, *from_string<net::ip_endpoint>("127.0.0.1:0"));
    auto server_endpoint = *server.socket.local_endpoint();

    spdlog::info("server listening at: {}", server_endpoint);

    sync_wait(
        [&]() -> task<void> {
            co_await web::async_serve(server, stop_source.get_token(), [&] {
                return [&stop_source]<typename Session>(Session session, std::stop_token stop_token) -> task<void> {
                    scope_guard _ = [&]() noexcept {//
                        stop_source.request_stop();
                    };
                    std::string rx_body;
                    while (net::has_pending_data(session)) {
                        auto message = co_await net::async_recv(session);
                        co_await net::async_recv(message, std::back_inserter(rx_body));
                        if (rx_body.size()) {
                            spdlog::info("body: {}", rx_body);
                            co_await net::async_send(session, as_bytes(std::span{rx_body.data(), rx_body.size()}));
                        }
                        rx_body.clear();
                    }
                    spdlog::info("session closed: {}", to_string(session.status()));
                };
            });
        }(),
        [&]() -> task<void> {
            auto session = co_await net::async_connect(ctx, web::proto::ws, server_endpoint);
            std::string total_body;
            co_await net::async_send(session, [&](auto &message) -> task<> {
                std::string tmp;
                for (size_t ii = 0; ii < 10; ++ii) {
                    tmp = make_random_string(300);
                    co_await net::async_send(message, tmp);
                    total_body += tmp;
                }
            });
            auto response = co_await net::async_recv(session);
            std::string rx_body;
            co_await net::async_recv(response, std::back_inserter(rx_body));
            co_await net::async_close(session);
            spdlog::info("client rx body: {}", rx_body);
            REQUIRE(rx_body == total_body);
        }(),
        async_exec(ctx, stop_source.get_token()));
}
