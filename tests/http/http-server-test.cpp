#include <catch2/catch.hpp>
#include <spdlog/spdlog.h>

#include <g6/http/client.hpp>
#include <g6/http/server.hpp>
#include <g6/io/context.hpp>

#include <unifex/sync_wait.hpp>
#include <unifex/task.hpp>
#include <unifex/when_all.hpp>

using namespace g6;

TEST_CASE("http simple server", "[g6::net::http]") {
    io::context ctx{};
    inplace_stop_source stop_source{};

    auto server = make_server(ctx, web::proto::http, *net::ip_endpoint::from_string("127.0.0.1:0"));
    auto server_endpoint = *server.local_endpoint();
    spdlog::info("server listening at: {}", server_endpoint.to_string());

    sync_wait(when_all(
        [&]() -> task<void> {
            co_await async_serve(server, stop_source, [&]<typename Session>(Session &session) {
                return [&session]<typename Request>(Request &request) -> task<void> {
                    do {
                        auto body = co_await net::async_recv(request);
                        auto sv_body = std::string_view{reinterpret_cast<char *>(body.data()), body.size()};
                        spdlog::info("body: {}", sv_body);
                        REQUIRE(sv_body == "Hello !");
                    } while (!request);
                    co_await net::async_send(session, http::status::ok, "OK !");
                };
            });
        }(),
        [&]() -> task<void> {
            scope_guard _ = [&]() noexcept { stop_source.request_stop(); };
            auto session = co_await net::async_connect(ctx, web::proto::http, server_endpoint);
            auto &response = co_await net::async_send(session, "/", http::method::post, "Hello !");

            std::string body_str;
            while (!response) {
                auto body = co_await net::async_recv(response);
                body_str += std::string_view{reinterpret_cast<char *>(body.data()), body.size()};
                spdlog::info("body: {}", body_str);
            }
            REQUIRE(response.status_code() == http::status::ok);
            REQUIRE(body_str == "OK !");
        }(),
        [&]() -> task<void> {
            ctx.run(stop_source.get_token());
            co_return;
        }()));
    // cleanup operation is not stoppable it must be done separately
    sync_wait(cleanup(server));
}
