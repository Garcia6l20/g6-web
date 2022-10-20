# g6 web library

> A g6-coro web framework

## Simple server example

```cpp
auto server = 
  make_server(ctx, web::proto::http, net::ipv4_endpoint{{127, 0, 0, 1}, 4242});
auto server_endpoint = *server.socket.local_endpoint();
spdlog::info("server listening at: {}", server_endpoint);
co_await web::async_serve(server, [&] {
    return [&]<typename Session, typename Request>(Session &session, Request request) -> task<> {
        std::string body;
        co_await net::async_recv(request, std::back_inserter(body));
        spdlog::info("body: {}", body);
        co_await net::async_send(session, body, http::status::ok);
    }
});
```

## Simple client example

```cpp
auto session =
    co_await net::async_connect(ctx, web::proto::http,
                                net::ipv4_endpoint{{127, 0, 0, 1}, 4242});
auto response =
    co_await net::async_send(client, std::string_view{"Hello !"}, "/", http::method::post);
spdlog::info("status: {}", response.get_status());
std::string body;
co_await net::async_recv(response, std::back_inserter(body));
spdlog::info("body: {}", body);
```

## More complex examples:

- [File server example](examples/file_server): chunked transfers example.
- [Chat server example](examples/chat): websocket example.


## Build

:warning: Dont try to build it directly, use [g6](https://github.com/Garcia6l20/g6) superproject instead !
Enable it with by setting *G6_WITH_WEB* cache variable to *ON*.

## Features

- [x] HTTP 1.1 server
- [x] HTTP 1.1 client
- [x] HTTP router
- [x] Chunked transfers
- [x] Websocket server
- [x] Websocket client
