#include <catch2/catch.hpp>
#include <spdlog/spdlog.h>

#include <g6/web/context.hpp>

#include <g6/http/client.hpp>
#include <g6/http/server.hpp>

#include <g6/ssl/certificate.hpp>
#include <g6/ssl/key.hpp>

#include <g6/scope_guard.hpp>
#include <g6/sync_wait.hpp>

#include <cert.hpp>

using namespace g6;

TEST_CASE("https simple server", "[g6::web::https]") {
    web::context ctx{};
    std::stop_source stop_source{};
    const ssl::certificate certificate{cert};
    const ssl::private_key private_key{key};

    auto server = web::make_server(ctx, web::proto::https, *net::ip_endpoint::from_string("127.0.0.1:0"), certificate,
                                   private_key);
    auto server_endpoint = *server.socket.local_endpoint();
    server.socket.host_name("localhost");
    server.socket.set_peer_verify_mode(ssl::peer_verify_mode::optional);
    server.socket.set_verify_flags(ssl::verify_flags::allow_untrusted);

    spdlog::info("server listening at: {}", server_endpoint.to_string());

    sync_wait(
        [&]() -> task<void> {
            co_await web::async_serve(server, stop_source, [&] {
                return [&]<typename Session, typename Request>(Session &session, Request request) -> task<void> {
                    while (net::has_pending_data(request)) {
                        auto body = co_await net::async_recv(request);
                        auto sv_body = std::string_view{reinterpret_cast<const char *>(body.data()), body.size()};
                        spdlog::info("body: {}", sv_body);
                        REQUIRE(sv_body == "Hello !");
                    }
                    co_await net::async_send(session, http::status::ok, as_bytes(std::span{"OK !", 4}));
                };
            });
        }(),
        [&]() -> task<void> {
            scope_guard _ = [&]() noexcept { stop_source.request_stop(); };
            auto session = co_await net::async_connect(ctx, web::proto::https, server_endpoint,
                                                       ssl::verify_flags::allow_untrusted);
            auto response =
                co_await net::async_send(session, "/", http::method::post, as_bytes(std::span{"Hello !", 7}));

            std::string body_str;
            while (net::has_pending_data(response)) {
                auto body = co_await net::async_recv(response);
                body_str += std::string_view{reinterpret_cast<char *>(body.data()), body.size()};
                spdlog::info("body: {}", body_str);
            }
            REQUIRE(response.status_code() == http::status::ok);
            REQUIRE(body_str == "OK !");
        }(),
        async_exec(ctx, stop_source.get_token()));
}
