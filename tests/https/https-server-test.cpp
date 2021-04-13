#include <catch2/catch.hpp>
#include <spdlog/spdlog.h>

#include <g6/io/context.hpp>

#include <g6/http/client.hpp>
#include <g6/http/server.hpp>

#include <g6/ssl/certificate.hpp>
#include <g6/ssl/key.hpp>

#include <unifex/sync_wait.hpp>
#include <unifex/task.hpp>
#include <unifex/when_all.hpp>

#include <cert.hpp>

using namespace g6;

TEST_CASE("https simple server", "[g6::net::https]") {
    io::context ctx{};
    inplace_stop_source stop_source{};
    const ssl::certificate certificate{cert};
    const ssl::private_key private_key{key};

    auto server = make_server(ctx, web::proto::https, *net::ip_endpoint::from_string("127.0.0.1:0"), certificate, private_key);
    auto server_endpoint = *server.socket.local_endpoint();
    server.socket.host_name("localhost");
    server.socket.set_peer_verify_mode(ssl::peer_verify_mode::optional);
    server.socket.set_verify_flags(ssl::verify_flags::allow_untrusted);

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
              co_await net::async_send(session, http::status::ok, as_bytes(span{"OK !", 4}));
            };
          });
        }(),
        [&]() -> task<void> {
          scope_guard _ = [&]() noexcept { stop_source.request_stop(); };
          auto session = co_await net::async_connect(ctx, web::proto::https, server_endpoint,
                                                     ssl::verify_flags::allow_untrusted);
          auto &response = co_await net::async_send(session, "/", http::method::post, as_bytes(span{"Hello !", 7}));

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
}
