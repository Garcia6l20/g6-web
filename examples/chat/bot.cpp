#include <spdlog/spdlog.h>

#include <g6/http/client.hpp>
#include <g6/web/context.hpp>
#include <g6/web/uri.hpp>
#include <g6/ws/client.hpp>

#include <g6/router.hpp>

#include <g6/coro/sync_wait.hpp>
#include <g6/coro/async_with.hpp>
#include <g6/scope_guard.hpp>


using namespace g6;

namespace {
    std::stop_source g_stop_source{};
}

std::string_view as_string_view(std::span<std::byte const> const &data) noexcept {
    return {reinterpret_cast<char const *>(data.data()), data.size()};
}

#include <csignal>

void term_handler(int) {
    g_stop_source.request_stop();
    spdlog::info("stop requested !");
}

int main(int, char **) {

    std::signal(SIGINT, term_handler);
    std::signal(SIGTERM, term_handler);

    spdlog::set_level(spdlog::level::debug);

    web::context ctx{};

    auto endpoint = web::uri{"http://127.0.0.1:8080"}.endpoint();
    if (!endpoint) {
        spdlog::error("invalid url !!!");
        return -1;
    }

    spdlog::info("running bot for server at: {}", *endpoint);

    using session_t = ws::client<web::context, net::async_socket>;

    auto responder = router::router{
        std::make_tuple(),
        g6::router::on<R"(say my name\s*(\w+)?)">([](std::string extra, router::context<session_t> session) -> task<> {
            std::string body_str;
            if (extra.size()) {
                body_str = fmt::format("Heisenber, {} !", extra);
            } else {
                body_str = fmt::format("Heisenber !");
            }
            co_await net::async_send(*session, body_str);
        }),
        g6::router::on<R"((.*))">([](std::string body_str, router::context<session_t> session) -> task<> {
            if (body_str.size()) {
                co_await net::async_send(*session, body_str);
            }
        }),
    };

    sync_wait(
        [&]() -> task<> {
            scope_guard _{[&] { g_stop_source.request_stop(); }};
            auto session = co_await net::async_connect(ctx, web::proto::ws, *endpoint, "/chat");

            while (true) {
                auto response = co_await net::async_recv(session);
                std::string body;
                co_await net::async_recv(response, std::back_inserter(body));
                co_await responder(body, std::ref(session));
            }

            spdlog::info("done");
        }() | async_with(g_stop_source.get_token()),
        async_exec(ctx));

    return 0;
}