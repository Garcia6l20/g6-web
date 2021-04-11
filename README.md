# g6 web library
> An unifex-based web framework

## Simple server example

```cpp
auto server = 
  make_server(ctx,
              web::proto::http,
              *net::ip_endpoint::from_string("127.0.0.1:4242"))
co_await async_serve(
  std::move(server), server_stop_source.get_token(),
  [&]<typename Session>(Session &session) {
    return [&session]<typename Request>(Request &request) -> task<void> {
      do {
        auto body = co_await net::async_recv(request);
        spdlog::info("body: {}", std::string_view{
            reinterpret_cast<char *>(body.data()), body.size()});
      } while (!request);
      co_await net::async_send(session, http::status::ok, "OK !");
    };
  );
```

## Simple client example

```cpp
auto session =
    co_await net::async_connect(ctx, web::proto::http,
                                *net::ip_endpoint::from_string("127.0.0.1:4242"));
auto &response = co_await net::async_send(
    session, "/", http::method::post, "Hello !");

std::string body_str;
while (!response) {
  auto body = co_await net::async_recv(response);
  body_str += std::string_view{reinterpret_cast<char *>(body.data()),
                               body.size()};
}
spdlog::info("body: {}", body_str);
```

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
- [x] Chunked transfers (server-side)
- [ ] Chunked transfers (client-side)
- [ ] Websocket server
- [ ] Websocket client

## Examples

- [file server](./examples/file_server/main.cpp): an http file server.
