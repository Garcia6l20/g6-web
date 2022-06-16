#include <catch2/catch.hpp>
#include <spdlog/spdlog.h>


#include <g6/web/context.hpp>

#include <g6/coro/sync_wait.hpp>
#include <g6/http/client.hpp>
#include <g6/http/server.hpp>
#include <g6/io/context.hpp>
#include <g6/scope_guard.hpp>

using namespace g6;
using namespace std::chrono_literals;

TEST_CASE("http server stop", "[g6::web::http]") try {
    spdlog::set_level(spdlog::level::debug);
    web::context ctx{};
    std::stop_source stop_source{};

    auto server = web::make_server(ctx, web::proto::http, *from_string<net::ip_endpoint>("127.0.0.1:0"));
    auto server_endpoint = *server.socket.local_endpoint();
    spdlog::info("server listening at: {}", server_endpoint);

    sync_wait(
        [&]() -> task<> {
            co_await web::async_serve(server, stop_source.get_token(), [&] {
                return []<typename Session, typename Request>(Session &session, Request request) -> task<> {
                    FAIL("Should not be reached !");
                    co_return;
                };
            });
            spdlog::info("server task terminated");
        }(),
        [&]() -> task<> {
            co_await g6::schedule_after(ctx, 50ms);
            stop_source.request_stop();
            spdlog::info("stop requested");
        }(),
        g6::async_exec(ctx, stop_source.get_token()));

    spdlog::info("done");
} catch (std::exception const &error) { FAIL(error.what()); }

TEST_CASE("http-server: basic request/response", "[g6::web::http]") {
    spdlog::set_level(spdlog::level::debug);
    web::context ctx{};
    std::stop_source stop_server{};
    std::stop_source stop{};

    auto server = web::make_server(ctx, web::proto::http, *g6::from_string<net::ip_endpoint>("127.0.0.1:0"));
    auto server_endpoint = *server.socket.local_endpoint();
    spdlog::info("server listening at: {}", server_endpoint);

    sync_wait(
        [&]() -> task<> {
            scope_guard _ = [&]() noexcept {//
                stop.request_stop();
            };
            co_await web::async_serve(server, stop_server.get_token(), [&] {
                return [&]<typename Session, typename Request>(Session &session, Request request) -> task<> {
                    for (auto cookie : request.cookies()) {
                        spdlog::info("cookie: {}={}", cookie.first, cookie.second);
                    }
                    std::string body;
                    co_await net::async_recv(request, std::back_inserter(body));
                    spdlog::info("body: {}", body);
                    REQUIRE(body == "Hello !");
                    http::headers hdrs{{"Set-Cookie", "one=1"}, {"Set-Cookie", "two=2"}};
                    co_await net::async_send(session, std::string_view{"OK !"}, http::status::ok, std::move(hdrs));
                };
            });
        }(),
        [&]() -> task<> {
            scope_guard _ = [&]() noexcept {//
                stop_server.request_stop();
            };
            auto client = co_await net::async_connect(ctx, web::proto::http, server_endpoint);
            co_await g6::schedule_after(ctx, 100ms);
            http::headers hdrs{{"Cookie", "one=1; two=2"}};
            auto response = co_await net::async_send(client, std::string_view{"Hello !"}, "/", http::method::post,
                                                     std::move(hdrs));
            std::string body;
            co_await net::async_recv(response, std::back_inserter(body));
            for (auto set_cookie : response.get_headers("Set-Cookie")) {//
                spdlog::info("set-cookie: {}", set_cookie);
            }
            spdlog::info("body: {}", body);
            REQUIRE(response.status_code() == http::status::ok);
            REQUIRE(body == "OK !");
        }(),
        g6::async_exec(ctx, stop.get_token()));
}

TEST_CASE("http-server: chunked request/response", "[g6::web::http]") {
    spdlog::set_level(spdlog::level::debug);
    web::context ctx{};
    std::stop_source stop_server{};
    std::stop_source stop{};

    auto server = web::make_server(ctx, web::proto::http, *g6::from_string<net::ip_endpoint>("127.0.0.1:0"));
    auto server_endpoint = *server.socket.local_endpoint();
    spdlog::info("server listening at: {}", server_endpoint);

    sync_wait(
        [&]() -> task<> {
            scope_guard _ = [&]() noexcept {//
                stop.request_stop();
            };
            co_await web::async_serve(server, stop_server.get_token(), [&] {
                return [&]<typename Session, typename Request>(Session &session, Request request) -> task<> {
                    std::string body;
                    co_await net::async_recv(request, std::back_inserter(body));
                    spdlog::info("body: {}", body);
                    REQUIRE(body == "Hello world !!");
                    http::headers hdrs{{"Set-Cookie", "one=1"}, {"Set-Cookie", "two=2"}};
                    auto message = co_await web::async_message(session, http::status::ok, std::move(hdrs));
                    co_await net::async_send(message, std::string_view{"Ola "});
                    co_await net::async_send(message, std::string_view{"el "});
                    co_await net::async_send(message, std::string_view{"mundo !!"});
                    co_await net::async_close(message);
                };
            });
        }(),
        [&]() -> task<> {
            scope_guard _ = [&]() noexcept {//
                stop_server.request_stop();
            };
            auto client = co_await net::async_connect(ctx, web::proto::http, server_endpoint);
            co_await g6::schedule_after(ctx, 100ms);
            http::headers hdrs{{"Cookie", "one=1; two=2"}};
            auto message = co_await web::async_message(client, "/", http::method::post, std::move(hdrs));
            co_await net::async_send(message, std::string_view{"Hello "});
            co_await net::async_send(message, std::string_view{"world "});
            co_await net::async_send(message, std::string_view{"!!"});
            auto response = co_await net::async_close(message);
            std::string body;
            co_await net::async_recv(response, std::back_inserter(body));
            spdlog::info("body: {}", body);
            REQUIRE(response.status_code() == http::status::ok);
            REQUIRE(body == "Ola el mundo !!");
        }(),
        g6::async_exec(ctx, stop.get_token()));
}
