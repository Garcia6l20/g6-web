#include <catch2/catch.hpp>
#include <spdlog/spdlog.h>

#include <g6/web/context.hpp>

#include <g6/ws/client.hpp>
#include <g6/ws/server.hpp>

#include <g6/coro/sync_wait.hpp>
#include <g6/scope_guard.hpp>

using namespace g6;

TEST_CASE("ws simple server", "[g6::web::ws]") {
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
                    auto message = co_await net::async_recv(session);
                    while (net::has_pending_data(message)) {
                        std::array<std::byte, 256> data;
                        size_t sz = co_await net::async_recv(message, data);
                        auto sv_body = std::string_view{reinterpret_cast<const char *>(data.data()), sz};
                        spdlog::info("body: {}", sv_body);
                        REQUIRE(sv_body == "Hello !");
                    }
                    co_await net::async_send(session, as_bytes(std::span{"OK !", 4}));
                };
            });
        }(),
        [&]() -> task<void> {
            auto session = co_await net::async_connect(ctx, web::proto::ws, server_endpoint);
            co_await net::async_send(session, as_bytes(std::span{"Hello !", 7}));
            auto message = co_await net::async_recv(session);
            std::string body_str;
            while (net::has_pending_data(message)) {
                std::array<std::byte, 256> data;
                size_t sz = co_await net::async_recv(message, data);
                auto body_sv = std::string_view{reinterpret_cast<const char *>(data.data()), sz};
                body_str += body_sv;
            }
            spdlog::info("body: {}", body_str);
            REQUIRE(body_str == "OK !");
        }(),
        async_exec(ctx, stop_source.get_token()));
}

// #include <random>

// std::string make_random_string(size_t size) noexcept {
//     static constexpr char alphanum[] =
//         "0123456789"
//         "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
//         "abcdefghijklmnopqrstuvwxyz";
//     std::string result;
//     result.reserve(size);
//     std::mt19937 rng{time(nullptr) * getpid()};
//     std::ranges::generate_n(std::back_inserter(result), size, [&]() {
//         return alphanum[rand() % (sizeof(alphanum) - 1)];
//     });
//     return result;
// }

// TEST_CASE("ws simple server: segmented", "[g6::web::ws]") {
//     spdlog::set_level(spdlog::level::debug);

//     web::context ctx{};
//     std::stop_source stop_source{};

//     auto server = web::make_server(ctx, web::proto::ws, *from_string<net::ip_endpoint>("127.0.0.1:0"));
//     auto server_endpoint = *server.socket.local_endpoint();

//     spdlog::info("server listening at: {}", server_endpoint);

//     sync_wait(
//         [&]() -> task<void> {
//             co_await web::async_serve(server, stop_source.get_token(), [&] {
//                 return [&stop_source]<typename Session, typename Request>(Session &session,
//                                                                           Request request) -> task<void> {
//                     scope_guard _ = [&]() noexcept { stop_source.request_stop(); };
//                     std::string rx_body;
//                     while (net::has_pending_data(request)) {
//                         auto body = co_await net::async_recv(request);
//                         rx_body += std::string_view{reinterpret_cast<const char *>(body.data()), body.size()};
//                     }
//                     spdlog::info("body: {}", rx_body);
//                     co_await net::async_send(session, as_bytes(std::span{rx_body.data(), rx_body.size()}));
//                 };
//             });
//         }(),
//         [&]() -> task<void> {
//             auto session = co_await net::async_connect(ctx, web::proto::ws, server_endpoint);
//             const std::string tx_body = make_random_string(1021);
//             co_await net::async_send(session, as_bytes(std::span{tx_body.data(), tx_body.size()}));
//             auto response = co_await net::async_recv(session);
//             std::string rx_body;
//             while (net::has_pending_data(response)) {
//                 auto body = co_await net::async_recv(response);
//                 rx_body += std::string_view{reinterpret_cast<const char *>(body.data()), body.size()};
//             }
//             spdlog::info("client rx body: {}", rx_body);
//             REQUIRE(rx_body == tx_body);
//         }(),
//         async_exec(ctx, stop_source.get_token()));
// }