#include <spdlog/spdlog.h>

#include <g6/web/context.hpp>
#include <g6/web/uri.hpp>

#include <g6/ws/client.hpp>
#include <g6/ws/server.hpp>

#include <g6/coro/sync_wait.hpp>
#include <g6/coro/when_all.hpp>
#include <g6/scope_guard.hpp>


using namespace g6;
using namespace std::chrono_literals;

std::string_view as_string_view(std::span<std::byte const> const &data) noexcept {
    return {reinterpret_cast<char const *>(data.data()), data.size()};
}

int main(int, char **) {
    spdlog::set_level(spdlog::level::debug);

    web::context ctx{};
    auto endpoint = web::uri{"http://127.0.0.1:8765"}.endpoint();
    if (!endpoint) {
        spdlog::error("invalid url !!!");
        return -1;
    }

    spdlog::info("running ping pong client for server at: {}", *endpoint);

    std::stop_source stop_source{};

    sync_wait(
        [&]() -> task<> {
            scope_guard _{[&] {//
                stop_source.request_stop();
            }};
            auto session = co_await net::async_connect(ctx, web::proto::ws, *endpoint, "/");
            constexpr std::string_view ping = "ping";
            co_await net::async_send(session, as_bytes(std::span{ping.data(), ping.size()}));
            auto response = co_await net::async_recv(session);
            std::string rx_body;
            co_await net::async_recv(response, std::back_inserter(rx_body));
            spdlog::info("client rx body: {}", rx_body);
            co_await net::async_close(session);
        }(),
        async_exec(ctx, stop_source.get_token()));

    return 0;
}
