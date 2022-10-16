#include <catch2/catch.hpp>
#include <spdlog/spdlog.h>

#include <g6/web/context.hpp>

#include <g6/http/client.hpp>
#include <g6/http/server.hpp>

#include <g6/ssl/certificate.hpp>
#include <g6/ssl/key.hpp>

#include <g6/coro/sync_wait.hpp>
#include <g6/coro/async_with.hpp>
#include <g6/scope_guard.hpp>

#include <cert.hpp>

using namespace g6;

TEST_CASE("g6::web::https simple server", "[g6][web][https]") {

    spdlog::set_level(spdlog::level::debug);

    web::context ctx{};
    std::stop_source stop_server;
    const ssl::certificate certificate{cert};
    const ssl::private_key private_key{key};

    auto server = web::make_server(ctx, web::proto::https, *from_string<net::ip_endpoint>("127.0.0.1:0"), certificate,
                                   private_key);
    auto server_endpoint = *server.socket.local_endpoint();
    server.socket.host_name("localhost");
    server.socket.set_peer_verify_mode(ssl::peer_verify_mode::optional);
    server.socket.set_verify_flags(ssl::verify_flags::allow_untrusted);

    spdlog::info("server listening at: {}", server_endpoint);

    sync_wait(
        [&]() -> task<void> {
            co_await web::async_serve(server, [&] {
                return [&]<typename Session, typename Request>(Session &session, Request request) -> task<void> {
                    std::string body;
                    co_await net::async_recv(request, std::back_inserter(body));
                    co_await net::async_send(session, std::string_view{"OK !"}, http::status::ok);
                };
            });
        }() | async_with(stop_server.get_token()),
        [&]() -> task<void> {
            scope_guard _ = [&]() noexcept { stop_server.request_stop(); };
            auto session = co_await net::async_connect(ctx, web::proto::https, server_endpoint,
                                                       ssl::verify_flags::allow_untrusted);
            auto response =
                co_await net::async_send(session, std::string_view{"Hello !"}, "/", http::method::post);

            std::string body;
            co_await net::async_recv(response, std::back_inserter(body));
            spdlog::info("body: {}", body);
            REQUIRE(response.get_status() == http::status::ok);
            REQUIRE(body == "OK !");
        }(),
        async_exec(ctx));
}
