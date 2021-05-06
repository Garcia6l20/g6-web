#include <catch2/catch.hpp>
#include <spdlog/spdlog.h>

#include <g6/io/context.hpp>

#include <g6/ws/client.hpp>
#include <g6/ws/server.hpp>

#include <unifex/sync_wait.hpp>
#include <unifex/task.hpp>
#include <unifex/when_all.hpp>

using namespace g6;

TEST_CASE("ws simple server", "[g6::web::ws]") {
    spdlog::set_level(spdlog::level::debug);

    io::context ctx{};
    inplace_stop_source stop_source{};

    auto server = web::make_server(ctx, web::proto::ws, *net::ip_endpoint::from_string("127.0.0.1:0"));
    auto server_endpoint = *server.socket.local_endpoint();

    spdlog::info("server listening at: {}", server_endpoint.to_string());

    sync_wait(when_all(
        [&]() -> task<void> {
            co_await web::async_serve(server, stop_source, [&]<typename Session>(Session &session) {
                return [&session, &stop_source]<typename Request>(Request request) -> task<void> {
                    scope_guard _ = [&]() noexcept { stop_source.request_stop(); };
                    while (net::has_pending_data(request)) {
                        auto body = co_await net::async_recv(request);
                        auto sv_body = std::string_view{reinterpret_cast<const char *>(body.data()), body.size()};
                        spdlog::info("body: {}", sv_body);
                        REQUIRE(sv_body == "Hello !");
                    }
                    co_await net::async_send(session, as_bytes(span{"OK !", 4}));
                };
            });
        }(),
        [&]() -> task<void> {
            auto session = co_await net::async_connect(ctx, web::proto::ws, server_endpoint);
            co_await net::async_send(session, as_bytes(span{"Hello !", 7}));
            auto response = co_await net::async_recv(session);
            std::string body_str;
            while (net::has_pending_data(response)) {
                auto body = co_await net::async_recv(response);
                auto body_sv = std::string_view{reinterpret_cast<const char *>(body.data()), body.size()};
                body_str += body_sv;
                spdlog::info("body: {}", body_sv);
            }
            REQUIRE(body_str == "OK !");
        }(),
        [&]() -> task<void> {
            ctx.run(stop_source.get_token());
            co_return;
        }()));
}
