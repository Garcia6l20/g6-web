#include <g6/web/uri.hpp>

#include <catch2/catch.hpp>
#include <spdlog/spdlog.h>

using namespace g6;

TEST_CASE("g6::web::uri: address resolution", "[g6][web]") {
    web::uri uri{"http://google.com:80"};
    REQUIRE(uri.scheme == "http");
    REQUIRE(uri.host == "google.com");
    auto ep = uri.endpoint();
    REQUIRE(ep);
    REQUIRE(ep->port() == 80);    
    spdlog::info("google endpoint: {}", *ep);
}
