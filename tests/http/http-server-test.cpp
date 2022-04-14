#include <catch2/catch.hpp>
#include <spdlog/spdlog.h>


#include <g6/web/context.hpp>

#include <g6/http/client.hpp>
#include <g6/http/server.hpp>
#include <g6/io/context.hpp>
#include <g6/scope_guard.hpp>
#include <g6/sync_wait.hpp>

using namespace g6;
using namespace std::chrono_literals;

TEST_CASE("http server stop", "[g6::web::http]") {
    spdlog::set_level(spdlog::level::debug);
    web::context ctx{};
    std::stop_source stop_source{};

    auto server = web::make_server(ctx, *net::ip_endpoint::from_string("127.0.0.1:0"));
    auto server_endpoint = *server.socket.local_endpoint();
    spdlog::info("server listening at: {}", server_endpoint.to_string());

    sync_wait(
        [&]() -> task<> {
            REQUIRE_THROWS_WITH(co_await web::async_serve(server, stop_source,
                                                          [&] {
                                                              return []<typename Session, typename Request>(
                                                                         Session &session, Request request) -> task<> {
                                                                  FAIL("Should not be reached !");
                                                                  co_return;
                                                              };
                                                          }),
                                "Operation canceled");
        }(),
        [&]() -> task<> {
            co_await g6::schedule_after(ctx, 50ms);
            stop_source.request_stop();
        }(),
        g6::async_exec(ctx, stop_source.get_token()));
}


TEST_CASE("http simple server", "[g6::web::http]") {
    spdlog::set_level(spdlog::level::debug);
    web::context ctx{};
    std::stop_source stop_source{};

    auto server = web::make_server(ctx, *net::ip_endpoint::from_string("127.0.0.1:0"));
    auto server_endpoint = *server.socket.local_endpoint();
    spdlog::info("server listening at: {}", server_endpoint.to_string());

    sync_wait(
        [&]() -> task<> {
            REQUIRE_THROWS_WITH(
                co_await web::async_serve(
                    server, stop_source,
                    [&] {
                        return []<typename Session, typename Request>(Session &session, Request request) -> task<> {
                            while (net::has_pending_data(request)) {
                                auto body = co_await net::async_recv(request);
                                auto sv_body =
                                    std::string_view{reinterpret_cast<char const *>(body.data()), body.size()};
                                spdlog::info("body: {}", sv_body);
                                REQUIRE(sv_body == "Hello !");
                            }
                            co_await net::async_send(session, http::status::ok, as_bytes(std::span{"OK !", 4}));
                        };
                    }),
                "Operation canceled");
        }(),
        [&]() -> task<> {
            scope_guard _ = [&]() noexcept { stop_source.request_stop(); };
            auto client = co_await net::async_connect(ctx, web::proto::http, server_endpoint);
            co_await g6::schedule_after(ctx, 100ms);
            auto response =
                co_await net::async_send(client, "/", http::method::post, as_bytes(std::span{"Hello !", 7}));

            std::string body_str;
            while (net::has_pending_data(response)) {
                auto body = co_await net::async_recv(response);
                body_str += std::string_view{reinterpret_cast<char *>(body.data()), body.size()};
                spdlog::info("body: {}", body_str);
            }
            REQUIRE(response.status_code() == http::status::ok);
            REQUIRE(body_str == "OK !");
        }(),
        g6::async_exec(ctx, stop_source.get_token()));
}
