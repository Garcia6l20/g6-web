# g6 web library

> An unifex-based web framework

## Simple server example

```cpp
auto server = 
  make_server(ctx,
              web::proto::http,
              *net::ip_endpoint::from_string("127.0.0.1:4242"))
co_await async_serve(
  server, server_stop_source.get_token(),
  [&]<typename Session>(Session &session) {
    return [&session](auto request) -> task<void> {
      while (net::has_pending_data(request)) {
        auto body = co_await net::async_recv(request);
        spdlog::info("body: {}", std::string_view{
            reinterpret_cast<char *>(body.data()), body.size()});
      }
      co_await net::async_send(session, http::status::ok, as_bytes(span{"OK !", 4}));
    };
  );
```

## Simple client example

```cpp
auto session =
    co_await net::async_connect(ctx, web::proto::http,
                                *net::ip_endpoint::from_string("127.0.0.1:4242"));
auto response = co_await net::async_send(
    session, "/", http::method::post, as_bytes(span{"Hello !", 7}));

std::string body_str;
while (net::has_pending_data(response)) {
  auto body = co_await net::async_recv(response);
  body_str += std::string_view{reinterpret_cast<char *>(body.data()),
                               body.size()};
}
spdlog::info("body: {}", body_str);
```

## More complex examples:

- [File server example](examples/file_server): chunked transfers example.
- [Chat server example](examples/chat): websocket example.


## Build

```bash
mkdir build && cd build
conan install .. --build=missing
cmake ..
cmake --build
```

## Features

- [x] HTTP 1.1 server
- [x] HTTP 1.1 client
- [x] HTTP router
- [x] Chunked transfers (server download)
- [ ] Chunked transfers (client upload)
- [x] Websocket server
- [x] Websocket client
