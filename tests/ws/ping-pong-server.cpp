#include <spdlog/spdlog.h>

#include <g6/web/context.hpp>

#include <g6/ws/client.hpp>
#include <g6/ws/server.hpp>

#include <g6/coro/sync_wait.hpp>
#include <g6/coro/when_all.hpp>
#include <g6/scope_guard.hpp>


using namespace g6;
using namespace std::chrono_literals;


int main(int, char **) {
    spdlog::set_level(spdlog::level::debug);

    web::context ctx{};
    std::stop_source stop_source{};

    auto server = web::make_server(ctx, web::proto::ws, *from_string<net::ip_endpoint>("127.0.0.1:0"));
    auto server_endpoint = *server.socket.local_endpoint();

    spdlog::info("server listening at: {}", server_endpoint);

    sync_wait(
        [&]() -> task<void> {
            co_await web::async_serve(server, stop_source.get_token(), [&] {
                return [&]<typename Session>(Session session, std::stop_token) -> task<void> {
                    std::stop_source session_stop;
                    co_await when_all(
                        [&]() -> task<> {
                            while (not session_stop.stop_requested()) {
                                auto message = co_await net::async_recv(session);
                                std::string body;
                                while (net::has_pending_data(message)) {
                                    std::array<std::byte, 256> data;
                                    auto sz = co_await net::async_recv(message, data);
                                    if (sz) {
                                        auto sv_body =
                                            std::string_view{reinterpret_cast<const char *>(data.data()), *sz};
                                        body += sv_body;
                                    } else {
                                        spdlog::info("session terminated: {}", to_string(sz.error()));
                                        session_stop.request_stop();
                                        co_return;
                                    }
                                }
                                spdlog::info("body: {}", body);
                                if (body == "ping") {
                                    co_await net::async_send(session, as_bytes(std::span{"pong", 4}));
                                }
                            }
                        }()/*,
                        [&]() -> task<> {
                            auto next_ping = now(ctx) + 1s;                            
                            while (not session_stop.stop_requested()) {
                                co_await schedule_at(ctx, next_ping);
                                next_ping += 1s;
                                co_await net::async_send(session, as_bytes(std::span{"ping", 4}));
                            }
                        }()*/);
                    stop_source.request_stop();
                };
            });
        }(),
        async_exec(ctx, stop_source.get_token()));
}
