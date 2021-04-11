#include <spdlog/spdlog.h>

#include <g6/http/client.hpp>
#include <g6/http/router.hpp>
#include <g6/http/server.hpp>
#include <g6/io/context.hpp>

#include <unifex/scope_guard.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/task.hpp>
#include <unifex/when_all.hpp>

#include <ranges>

using namespace g6;
namespace fs = std::filesystem;

namespace rng = std::ranges;

static constexpr std::string_view list_dir_template_ = R"(
<!DOCTYPE html>
<html lang="en">
<head lang="en">
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<link rel="stylesheet" href="https://stackpath.bootstrapcdn.com/bootstrap/5.0.0-alpha1/css/bootstrap.min.css" integrity="sha384-r4NyP46KrjDleawBgD5tp8Y7UzmLA05oM1iAEQ17CSuDqnUK2+k9luXQOfXJCJ4I" crossorigin="anonymous">
<title>{title}</title>
</head>
<body class="bg-light">
    <div class="container">
        <div class="py-5 text-center">
            <h2>G6 File Server</h2>
            <p class="lead">{path}</p>
        </div>
        <nav aria-label="breadcrumb"><ol class="breadcrumb">{breadcrumb}</ol></nav>
        {body}
    </div>
    <script src="https://cdn.jsdelivr.net/npm/popper.js@1.16.0/dist/umd/popper.min.js" integrity="sha384-Q6E9RHvbIyZFJoft+2mJbHaEWldlvI9IOYy5n3zV9zzTtmI3UksdQRVvoxMfooAo" crossorigin="anonymous"></script>
    <script src="https://stackpath.bootstrapcdn.com/bootstrap/5.0.0-alpha1/js/bootstrap.min.js" integrity="sha384-oesi62hOLfzrys4LxRF63OJCXdXDipiYWBnvTl9Y9/TRlw5xlKIEHpNyvvDShgf/" crossorigin="anonymous"></script>
</body>
</html>
)";

fmt::memory_buffer make_body(const fs::path &root, std::string_view path) {
  auto link_path = fs::relative(path, root).string();
  if (link_path.empty())
    link_path = ".";
  fmt::memory_buffer out;
  fmt::format_to(out, R"(<div class="list-group">)");
  for (auto &p : fs::directory_iterator(root / path)) {
    fmt::format_to(
        out,
        R"(<a class="list-group-item-action" href="/{link_path}">{path}</a>)",
        fmt::arg("path",
                 fs::relative(p.path(), p.path().parent_path()).c_str()),
        fmt::arg("link_path", fs::relative(p.path(), root).c_str()));
  }
  fmt::format_to(out, "</div>");
  return out;
}

fmt::memory_buffer make_breadcrumb(std::string_view path) {
  fmt::memory_buffer buff;
  constexpr std::string_view init =
      R"(<li class="breadcrumb-item"><a href="/">Home</a></li>)";
  buff.append(std::begin(init), std::end(init));
  fs::path p = path;
  std::vector<std::string> elems = {{fmt::format(
      R"(<li class="breadcrumb-item active" aria-current="page"><a href="/{}">{}</a></li>)",
      p.string(), p.filename().c_str())}};
  p = p.parent_path();
  while (not p.filename().empty()) {
    elems.emplace_back(
        fmt::format(R"(<li class="breadcrumb-item"><a href="/{}">{}</a></li>)",
                    p.string(), p.filename().c_str()));
    p = p.parent_path();
  }
  for (auto &elem : elems | std::views::reverse) {
    fmt::format_to(buff, "{}", elem);
  }
  return buff;
}

int main(int argc, char **argv) {
  io::context context{};
  inplace_stop_source stop_source{};
  //  auto t = std::thread{[&] { context.run(stop_source.get_token()); }};
  //  auto at_exit = scope_guard{[&]() noexcept { stop_source.request_stop();
  //  }};
  auto server = make_server(context, web::proto::http,
                            *net::ip_endpoint::from_string("127.0.0.1:0"));
  auto server_endpoint = *server.local_endpoint();
  fs::path root_path = ".";
  spdlog::info("server listening at: http://{}", server_endpoint.to_string());

  auto router = router::router{
      std::make_tuple(), // global context
      http::route::get<R"(/(.*))">(
          [&](std::string_view path,
              router::context<http::server_session<net::async_socket>> session,
              router::context<http::server_session<net::async_socket>::request>
                  request) -> task<void> {
            spdlog::info("get: {}", path);
            if (fs::is_directory(root_path / path)) {
              fmt::memory_buffer body;
              try {
                body = make_body(root_path, path);
              } catch (fs::filesystem_error &error) {
                co_await net::async_send(
                    *session, http::status::not_found,
                    fmt::format(R"(<div><h6>Not found</h6><p>{}</p></div>)",
                                error.what()));
              }
              auto breadcrumb = make_breadcrumb(path);
              co_await net::async_send(
                  *session, http::status::ok,
                  fmt::format(list_dir_template_, fmt::arg("title", path),
                              fmt::arg("body", std::string_view{body.data(),
                                                                body.size()}),
                              fmt::arg("path", path),
                              fmt::arg("breadcrumb",
                                       std::string_view{breadcrumb.data(),
                                                        breadcrumb.size()})));
            } else {
              try {
                auto file = open_file_read_only(context.get_scheduler(),
                                                root_path / path);
                http::headers headers{{"Transfer-Encoding", "chunked"}};
                auto stream = co_await net::async_send(
                    *session, http::status::ok, std::move(headers));
                std::array<char, 1024> data{};
                size_t offset = 0;
                while (size_t bytes = co_await async_read_some_at(
                           file, offset, as_writable_bytes(span{data}))) {
                  offset += bytes;
                  co_await net::async_send(stream,
                                           as_bytes(span{data.data(), bytes}));
                }
                co_await net::async_send(stream); // close stream
              } catch (std::system_error &error) {
                co_await net::async_send(
                    *session, http::status::not_found,
                    fmt::format(R"(<div><h6>Not found</h6><p>{}</p></div>)",
                                error.what()));
              }
            }
          }),
      router::on<R"(.*)">(
          [](router::context<http::server_session<net::async_socket>> session,
             router::context<http::server_session<net::async_socket>::request>
                 request) -> task<void> {
            spdlog::info("unhandled: {} {}", request->url(), request->method());
            co_await net::async_send(*session, http::status::not_found,
                                     "Not found");
          })};
  sync_wait(when_all(
      [&]() -> task<void> {
        co_await async_serve(
            std::move(server), stop_source.get_token(),
            [&]<typename Session>(Session &session) {
              return [root_path, &session, &router]<typename Request>(
                         Request &request) mutable -> task<void> {
                if (request) {
                  co_await router(request.url(), request.method(),
                                  std::ref(request), std::ref(session));
                }
              };
            });
        spdlog::info("terminated !");
      }(),
      [&]() -> task<void> {
        context.run(stop_source.get_token());
        co_return;
      }()));
}
