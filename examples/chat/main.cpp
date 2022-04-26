#include <spdlog/spdlog.h>

#include <g6/http/client.hpp>
#include <g6/http/router.hpp>
#include <g6/http/server.hpp>
#include <g6/web/context.hpp>
#include <g6/ws/server.hpp>

#include <g6/sync_wait.hpp>

#include <ranges>

using namespace g6;
namespace fs = std::filesystem;

namespace rng = std::ranges;

namespace js {
    namespace detail {
#include <js_main.hpp>
        inline const std::string_view main{reinterpret_cast<const char *>(main_js), main_js_len};
    }// namespace detail
    using detail::main;
}// namespace js

namespace html {
    namespace detail {
#include <html_index.hpp>
        inline const std::string_view index{reinterpret_cast<const char *>(index_html), index_html_len};
    }// namespace detail
    using detail::index;
}// namespace html

namespace {
    std::stop_source g_stop_source{};
}

#include <csignal>
#include <list>

void terminate_handler(int) {
    g_stop_source.request_stop();
    spdlog::info("stop requested !");
}

std::string_view as_string_view(std::span<std::byte const> const &data) noexcept {
    return {reinterpret_cast<char const *>(data.data()), data.size()};
}

int main(int argc, char **argv) {
#ifdef G6_WEB_DEBUG
    spdlog::set_level(spdlog::level::debug);
#endif

    web::context context{};

    std::signal(SIGINT, terminate_handler);
    std::signal(SIGTERM, terminate_handler);

    auto server = web::make_server(context, web::proto::http, *net::ip_endpoint::from_string("127.0.0.1:0"));
    auto server_endpoint = *server.socket.local_endpoint();
    fs::path root_path = ".";
    spdlog::info("server listening at: http://{}", server_endpoint.to_string());

    g6::ff_spawner scope{};
    std::list<ws::server_session<net::async_socket> *> all_sessions{};

    auto router = router::router{
        std::make_tuple(),// global context
        http::route::get<R"(/)">([&](router::context<http::server_session<net::async_socket>> session,
                                     router::context<http::server_request<net::async_socket>> request) -> task<void> {
            auto page = fmt::vformat(html::index,
                                     fmt::make_format_args(fmt::arg("address", server_endpoint.address().to_string()),
                                                           fmt::arg("port", server_endpoint.port()),
                                                           fmt::arg("js_main", js::main)));
            co_await net::async_send(*session, http::status::ok, std::as_bytes(std::span{page.data(), page.size()}));
        }),
        http::route::get<R"(/chat)">([&](router::context<http::server_session<net::async_socket>> session,
                                         router::context<http::server_request<net::async_socket>> request,
                                         router::context<g6::ff_spawner> scope) -> task<void> {
            spdlog::info("got chat request on {}", session->remote_endpoint().to_string());
            auto ws_session = co_await web::upgrade_connection(web::proto::ws, *session, *request);
            spdlog::info("connection upgraded...");
            scope->spawn([](auto session, auto &all_sessions) -> task<void> {
                all_sessions.push_front(&session);
                spdlog::info("websocket connection started...");
                while (true) try {
                        auto message = co_await net::async_recv(session);
                        while (net::has_pending_data(message)) {
                            auto data = co_await net::async_recv(message);
                            spdlog::info("data from {}: {}", session.remote_endpoint().to_string(),
                                         as_string_view(data));
                            spdlog::info("{} other connections", std::size(all_sessions) - 1);
                            for (auto other_session : all_sessions) {
                                if (*other_session != session) {
                                    spdlog::info("sending data to: {}", other_session->remote_endpoint().to_string());
                                    co_await net::async_send(*other_session, data);
                                }
                            }
                        }
                    } catch (std::system_error const &error) {
                        if (error.code() != std::errc::connection_reset) {
                            spdlog::error("error on {}: {}", session.remote_endpoint().to_string(), error.what());
                        }
                        spdlog::error("connection reset: {}", session.remote_endpoint().to_string());
                        break;
                    }
                std::erase_if(all_sessions, [&](auto const &other) { return other == &session; });
                spdlog::info("{} remaining connections", std::size(all_sessions));
            }(std::move(ws_session), all_sessions));
            throw std::system_error{std::make_error_code(std::errc::connection_reset)};
        }),
        router::on<R"(.*)">([](router::context<http::server_session<net::async_socket>> session,
                               router::context<http::server_request<net::async_socket>> request) -> task<void> {
            spdlog::info("unhandled: {}", request->url());
            std::string_view not_found = R"(<div><h6>Not found</h6><p>{}</p></div>)";
            co_await net::async_send(*session, http::status::not_found,
                                     as_bytes(std::span{not_found.data(), not_found.size()}));
        })};
    sync_wait(
        [&]() -> task<void> {
            co_await web::async_serve(server, g_stop_source, [&] {
                return [root_path, &router, &scope = scope]<typename Session, typename Request>(
                           Session &session, Request request) mutable -> task<void> {
                    co_await router(request.url(), request.method(), std::ref(session), std::ref(request),
                                    std::ref(scope));
                    while (net::has_pending_data(request)) {
                        co_await net::async_recv(request);// flush unused body
                    }
                };
            });
            spdlog::info("terminated !");
        }(),
        async_exec(context, g_stop_source.get_token()));
}
