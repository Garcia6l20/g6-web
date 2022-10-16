#include <catch2/catch.hpp>
#include <spdlog/spdlog.h>

#include <g6/web/context.hpp>

#include <g6/ws/client.hpp>
#include <g6/ws/server.hpp>

#include <g6/coro/async_with.hpp>
#include <g6/coro/sync_wait.hpp>
#include <g6/scope_guard.hpp>

using namespace g6;

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

TEST_CASE("g6::web::ws simple server: segmented", "[g6][web][ws]") {
    spdlog::set_level(spdlog::level::debug);

    web::context ctx{};
    std::stop_source stop;

    auto server = web::make_server(ctx, web::proto::ws, *from_string<net::ip_endpoint>("127.0.0.1:0"));
    auto server_endpoint = *server.socket.local_endpoint();

    spdlog::info("server listening at: {}", server_endpoint);

    sync_wait(
        [&]() -> task<void> {
            co_await web::async_serve(server, [&] {
                return []<typename Session>(Session session) -> task<void> {
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
        }() | async_with(stop.get_token()),
        [&]() -> task<void> {
            scope_guard _ = [&]() noexcept {//
                stop.request_stop();
            };
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
        async_exec(ctx));
}

TEST_CASE("g6::web::ws simple server: segmented/generator like", "[g6][web][ws]") {
    spdlog::set_level(spdlog::level::debug);

    web::context ctx{};
    std::stop_source stop;

    auto server = web::make_server(ctx, web::proto::ws, *from_string<net::ip_endpoint>("127.0.0.1:0"));
    auto server_endpoint = *server.socket.local_endpoint();

    spdlog::info("server listening at: {}", server_endpoint);

    sync_wait(
        [&]() -> task<void> {
            co_await web::async_serve(server, [&] {
                return [&]<typename Session>(Session session) -> task<void> {
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
        }() | async_with(stop.get_token()),
        [&]() -> task<void> {
            scope_guard _ = [&]() noexcept {//
                spdlog::info("requesting stop...");
                stop.request_stop();
            };
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
        async_exec(ctx));
}

TEST_CASE("g6::web::ws concurrent", "[g6][web][ws]") {
    spdlog::set_level(spdlog::level::debug);

    web::context ctx{};
    std::stop_source stop_ctx, stop;

    auto server = web::make_server(ctx, web::proto::ws, *from_string<net::ip_endpoint>("127.0.0.1:0"));
    auto server_endpoint = *server.socket.local_endpoint();

    spdlog::info("server listening at: {}", server_endpoint);

    constexpr size_t n_bytes = 128 * 4 + 32;
    constexpr size_t n_buf = 10;
    constexpr size_t n_test = 10;
    constexpr size_t n_job = 10;

    auto test_id = 0;
    auto test = [](auto &session, auto id) -> task<> {
        std::string total_body;
        // NOTE: async_send returns an async lock
        //       that can be used to make sure
        //       no one else (from the same connection)
        //       will send data to the server...
        //       Here the test is broken if any other
        //       test sends data to the server since
        //       we await the response directly
        //       without any subprotocol.
        //       Some sub-protocols addresses this 'issue'
        //       by using id-mapped messaging queues or other mechanism
        //       (ie.: channels, ocpp, etc...)
        auto lock = co_await net::async_send(session, [&](auto &message) {
            return [](auto &message, auto &body, auto id) -> task<> {
                std::string tmp;
                for (size_t ii = 0; ii < n_buf; ++ii) {
                    tmp = make_random_string(n_bytes);
                    co_await net::async_send(message, tmp);
                    spdlog::trace("test {} sent: {}", id, tmp);
                    body += tmp;
                }
            }(message, total_body, id);
        });
        auto response = co_await net::async_recv(session);
        std::string rx_body;
        co_await net::async_recv(response, std::back_inserter(rx_body));
        spdlog::trace("test {} rx body: {}", id, rx_body);
        REQUIRE(rx_body == total_body);
    };

    auto job = [&]() -> task<> {
        auto session = co_await net::async_connect(ctx, web::proto::ws, server_endpoint);
        std::vector<task<>> tests{n_test};
        std::ranges::generate(tests, [&] { return test(session, ++test_id); });
        co_await when_all(std::move(tests));
        co_await net::async_close(session);
    };

    sync_wait(
        [&]() -> task<> {
            size_t n_session = 0;
            size_t n_message = 0;
            scope_guard _ = [&] {//
                REQUIRE(n_session == n_job);
                REQUIRE(n_message == n_job * n_test);
                stop_ctx.request_stop();
            };
            co_await web::async_serve(server, [&] {
                return [&]<typename Session>(Session session) -> task<void> {
                    auto id = ++n_session;
                    std::string rx_body;
                    while (net::has_pending_data(session)) {
                        auto message = co_await net::async_recv(session);
                        co_await net::async_recv(message, std::back_inserter(rx_body));
                        if (rx_body.size()) {
                            ++n_message;
                            spdlog::trace("session {} body: {}", id, rx_body);
                            co_await net::async_send(session, as_bytes(std::span{rx_body.data(), rx_body.size()}));
                        }
                        rx_body.clear();
                    }
                    spdlog::info("session closed: {}", to_string(session.status()));
                };
            });
        }() | async_with(stop.get_token()),
        [&]() -> task<> {
            scope_guard _ = [&]() noexcept {//
                spdlog::info("requesting stop...");
                stop.request_stop();
            };
            std::vector<task<>> jobs{n_job};
            std::ranges::generate(jobs, [&] { return job(); });
            co_await when_all(std::move(jobs));
        }(),
        async_exec(ctx, stop_ctx.get_token()));
}