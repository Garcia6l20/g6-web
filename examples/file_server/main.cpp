#include <spdlog/spdlog.h>

#include <g6/tmp.hpp>

// #include <g6/http/client.hpp>
#include <g6/http/router.hpp>
#include <g6/http/server.hpp>
#include <g6/web/context.hpp>

#include <g6/sync_wait.hpp>

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
<title>{{title}}</title>
</head>
<body class="bg-light">
    <div class="container">
        <div class="py-5 text-center">
            <h2>G6 File Server</h2>
            <p class="lead">{{path}}</p>
        </div>
        <nav aria-label="breadcrumb"><ol class="breadcrumb">
        <li class="breadcrumb-item"><a href="/">Home</a></li>
        {% for part in breadcrumb %}
        <li class="breadcrumb-item"><a href="/{{part.url}}">{{part.name}}</a></li>
        {% endfor %}
        </ol></nav>
        <div class="list-group">
        {% for directory in directories %}
        <a class="list-group-item-action" href="/{{directory.url}}">{{directory.path}}</a>
        {% endfor %}
        {% for file in files %}
        <a class="list-group-item-action{% if for.last %} active{% endif %}" href="/{{file.url}}">{{file.path}}</a>
        {% endfor %}
        </div>
    </div>
    <script src="https://cdn.jsdelivr.net/npm/popper.js@1.16.0/dist/umd/popper.min.js" integrity="sha384-Q6E9RHvbIyZFJoft+2mJbHaEWldlvI9IOYy5n3zV9zzTtmI3UksdQRVvoxMfooAo" crossorigin="anonymous"></script>
    <script src="https://stackpath.bootstrapcdn.com/bootstrap/5.0.0-alpha1/js/bootstrap.min.js" integrity="sha384-oesi62hOLfzrys4LxRF63OJCXdXDipiYWBnvTl9Y9/TRlw5xlKIEHpNyvvDShgf/" crossorigin="anonymous"></script>
</body>
</html>
)";

auto make_body(const fs::path &root, std::string_view path) {
    using namespace g6::poly::literals;

    auto dir = root / path;

    spdlog::info("iterating: {}", dir.string());

    poly::vec<std::string> files;
    poly::vec<std::string> dirs;
    for (auto &p : fs::directory_iterator(dir)) {

        spdlog::info("-- {}", p.path().string());
        auto from_root = fs::relative(p.path(), root).string();
        auto from_parent = fs::relative(p.path(), p.path().parent_path()).string();

        spdlog::info("-- rel from root: {}", from_root);
        spdlog::info("-- rel from parent: {}", from_parent);

        if (fs::is_directory(p)) {
            dirs.push_back(poly::var{poly::obj{"url"_kw = from_root, "path"_kw = from_parent}});
        } else {
            files.push_back(poly::var{poly::obj{"url"_kw = from_root, "path"_kw = from_parent}});
        }
    }
    return std::make_tuple(dirs, files);
}

auto make_breadcrumb(std::string_view path) {
    using namespace g6::poly::literals;

    fs::path p{path};
    poly::vec<std::string> items{};
    items.push_back(poly::var{poly::obj{"url"_kw = p.string(), "name"_kw = p.filename().string()}});
    p = p.parent_path();
    while (not p.filename().empty()) {
        items.push_back(poly::var{poly::obj{"url"_kw = p.string(), "name"_kw = p.filename().string()}});
        p = p.parent_path();
    }
    std::ranges::reverse(items);
    return items;
}

namespace {
    std::stop_source g_stop_source{};
}

#include <csignal>

void terminate_handler(int) {
    g_stop_source.request_stop();
    spdlog::info("stop requested !");
}

int main(int argc, char **argv) {
    web::context context{};
    tmp::engine temp{list_dir_template_};

    std::signal(SIGINT, terminate_handler);
    std::signal(SIGTERM, terminate_handler);
    std::signal(SIGUSR1, terminate_handler);

    auto server = web::make_server(context, web::proto::http, *net::ip_endpoint::from_string("127.0.0.1:0"));
    auto server_endpoint = *server.socket.local_endpoint();
    fs::path root_path = fs::current_path();

    if (argc > 1) { root_path = fs::path(argv[1]); }

    auto router = router::router{
        std::make_tuple(),// global context
        http::route::get<R"(/(.*))">([&](std::string_view path,
                                         router::context<http::server_session<net::async_socket>> session,
                                         router::context<http::server_request<net::async_socket>> request)
                                         -> task<void> {
            spdlog::info("get: {}", (root_path / path).string());
            if (fs::is_directory(root_path / path)) {
                using namespace g6::poly::literals;
                auto [dirs, files] = make_body(root_path, path);
                try {
                    auto page = temp("title"_kw = "Title", "path"_kw = "Path", "breadcrumb"_kw = make_breadcrumb(path),
                                     "directories"_kw = dirs, "files"_kw = files);
                    co_await net::async_send(*session, http::status::ok, as_bytes(std::span{page.data(), page.size()}));
                } catch (std::exception const &error) {
                    std::string_view err = error.what();
                    co_await net::async_send(*session, http::status::internal_server_error,
                                             std::as_bytes(std::span{err.data(), err.size()}));
                }
            } else if (fs::exists(root_path / path)) {
                spdlog::info("opening: {}", (root_path / path).string());
                auto file =
                    open_file(context, root_path / path, g6::open_file_mode::existing | g6::open_file_mode::read);
                http::headers headers{{"Transfer-Encoding", "chunked"}};
                auto stream = co_await net::async_send(*session, http::status::ok, std::move(headers));
                std::array<char, 1024> data{};
                size_t offset = 0;
                while (size_t bytes = co_await async_read_some(file, as_writable_bytes(std::span{data}))) {
                    offset += bytes;
                    co_await net::async_send(stream, as_bytes(std::span{data.data(), bytes}));
                }
                co_await net::async_send(stream);// close stream
            } else {
                auto err_page =
                    fmt::format(R"(<div><h6>Not found</h6><p>No such file or directory: {}</p></div>)", path);
                co_await net::async_send(*session, http::status::not_found,
                                         std::as_bytes(std::span{err_page.data(), err_page.size()}));
            }
        }),
        router::on<R"(.*)">([](router::context<http::server_session<net::async_socket>> session,
                               router::context<http::server_request<net::async_socket>> request) -> task<void> {
            // spdlog::info("unhandled: {} {}", request->url(), request->method());
            constexpr std::string_view not_found = R"(<div><h6>Not found</h6></div>)";
            co_await net::async_send(*session, http::status::not_found,
                                     as_bytes(std::span{not_found.data(), not_found.size()}));
        })};
    sync_wait(
        [&]() -> task<void> {
            co_await async_write_some(context.cout, "server listening at: http://{}\n", server_endpoint.to_string());
            co_await web::async_serve(server, g_stop_source, [&] {
                return [root_path, &router, &context]<typename Session, typename Request>(
                           Session &session, Request request) mutable -> task<void> {
                    spdlog::info("{}", request.url());
                    co_await router(request.url(), request.method(), std::ref(request), std::ref(session));
                    while (net::has_pending_data(request)) {
                        co_await net::async_recv(request);// flush unused body
                    }
                    spdlog::info("done");
                };
            });
            spdlog::info("terminated !");
            co_return;
        }(),
        async_exec(context, g_stop_source.get_token()));
}
