#include <spdlog/spdlog.h>

#include <g6/json/json.hpp>

#include <g6/web/context.hpp>
#include <g6/http/router.hpp>
#include <g6/http/server.hpp>
#include <g6/ws/server.hpp>

#include <g6/coro/sync_wait.hpp>
#include <g6/coro/async_with.hpp>

#include <ranges>

using namespace g6;
namespace fs = std::filesystem;

namespace rng = std::ranges;

namespace js {
    namespace detail {
#include <main_js.hpp>
        inline const std::string_view main{reinterpret_cast<const char *>(main_js), main_js_len};
    }// namespace detail
    using detail::main;
}// namespace js

namespace html {
    namespace detail {
#include <index_html.hpp>
        inline const std::string_view index{reinterpret_cast<const char *>(index_html), index_html_len};
#include <login_html.hpp>
        inline const std::string_view login{reinterpret_cast<const char *>(login_html), login_html_len};
    }// namespace detail
    using detail::index;
    using detail::login;
}// namespace html

struct user_session {
    bool logged = false;
    std::string username{};
};

namespace {
    std::stop_source g_stop_source{};
}

#include <csignal>
#include <list>

void term_handler(int) {
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

    std::signal(SIGINT, term_handler);
    std::signal(SIGTERM, term_handler);

    auto server = web::make_server(context, web::proto::http, *from_string<net::ip_endpoint>("127.0.0.1:8080"));
    auto server_endpoint = *server.socket.local_endpoint();
    fs::path root_path = ".";
    spdlog::info("server listening at: http://{}", server_endpoint);

    std::list<ws::server_session<net::async_socket> *> all_sessions{};

    auto router = router::router{
        std::make_tuple(),// global context
        http::route::get<R"(/)">([&](router::context<http::server_session<net::async_socket>> session,
                                     router::context<http::server_request<net::async_socket>> request,
                                     router::context<user_session> us) -> task<void> {
            if (!us->logged) {
                http::headers hdrs{{"Location", "/login"}};// gcc bug ???
                co_await net::async_send(*session, http::status::temporary_redirect, std::move(hdrs));
            } else {
                auto page =
                    fmt::vformat(html::index, fmt::make_format_args(fmt::arg("address", server_endpoint.address()),
                                                                    fmt::arg("port", server_endpoint.port()),
                                                                    fmt::arg("js_main", js::main)));
                co_await net::async_send(*session, page, http::status::ok);
            }
        }),
        http::route::get<R"(/login)">(
            [&](router::context<http::server_session<net::async_socket>> session,
                router::context<http::server_request<net::async_socket>> request) -> task<void> {
                co_await net::async_send(*session, html::login, http::status::ok);
            }),
        http::route::post<R"(/login)">([&](router::context<http::server_session<net::async_socket>> session,
                                           router::context<http::server_request<net::async_socket>> request,
                                           router::context<user_session> us,
                                           router::context<std::string> body) -> task<void> {
            auto jsn = json::load(*body).get<json::object>();
            us->logged = true;
            us->username = jsn["username"].get<json::string>();
            std::string response_data = "ok";
            auto hdrs = http::headers{{"Set-Cookie", fmt::format("username={}", jsn["username"].get<json::string>())}};
            co_await net::async_send(*session, response_data, http::status::ok,
                                     std::move(hdrs));// gcc bug ? cannot be inplace-constructed
        }),
        http::route::get<R"(/chat)">([&](router::context<http::server_session<net::async_socket>> session,
                                         router::context<http::server_request<net::async_socket>> request) -> task<void> {
            auto cookies = request->cookies();
            if (not cookies.contains("username")) {
                co_await net::async_send(*session, http::status::unauthorized);
                co_return;
            }
            auto username = cookies.at("username");

            spdlog::info("got chat request on {} {}", session->remote_endpoint(), username);
            auto ws_session = co_await web::upgrade_connection(web::proto::ws, *session, *request);
            spdlog::info("connection upgraded...");
            spawn([](auto session, auto &all_sessions, const std::string username) -> task<void> {
                all_sessions.push_front(&session);
                spdlog::info("websocket connection started...");
                while (net::has_pending_data(session)) try {
                        auto message = co_await net::async_recv(session);
                        std::string body;
                        co_await net::async_recv(message, std::back_inserter(body));
                        spdlog::info("data from {} ({}): {}", username, session.remote_endpoint(), body);
                        spdlog::info("{} other connections", std::size(all_sessions) - 1);
                        using namespace poly::literals;
                        auto data_for_others = json::dump(json::object{"user"_kw = username, "message"_kw = body});
                        for (auto other_session : all_sessions) {
                            if (*other_session != session) {
                                spdlog::info("sending data to: {}", other_session->remote_endpoint());
                                co_await net::async_send(*other_session, data_for_others);
                            }
                        }
                    } catch (std::system_error const &error) {
                        if (error.code() != std::errc::connection_reset) {
                            spdlog::error("error on {}: {}", session.remote_endpoint(), error.what());
                        }
                        spdlog::error("connection reset: {}", session.remote_endpoint());
                        break;
                    }
                std::erase_if(all_sessions, [&](auto const &other) { return other == &session; });
                spdlog::info("{} remaining connections", std::size(all_sessions));
            }(std::move(ws_session), all_sessions, std::string{username.data(), username.size()}));
            throw std::system_error{std::make_error_code(std::errc::connection_reset)};
        }),
        router::on<R"(.*)">([](router::context<http::server_session<net::async_socket>> session,
                               router::context<http::server_request<net::async_socket>> request) -> task<void> {
            spdlog::info("unhandled: {}", request->get_path());
            std::string_view not_found = R"(<div><h6>Not found</h6><p>{}</p></div>)";
            co_await net::async_send(*session, not_found, http::status::not_found);
        })};
    sync_wait(
        [&]() -> task<void> {
            co_await web::async_serve(server, [&] {
                return [root_path, &router, us = user_session{}]<typename Session, typename Request>(
                           Session &session, Request request) mutable -> task<void> {
                    std::string body;
                    co_await net::async_recv(request, std::back_inserter(body));
                    co_await router(request.get_path(), request.get_method(), std::ref(session), std::ref(request),
                                    std::ref(us), std::ref(body));
                };
            });
            spdlog::info("terminated !");
        }() | async_with(g_stop_source.get_token()),
        async_exec(context, g_stop_source.get_token()));
}
