#pragma once

#include <g6/router.hpp>
#include <g6/http/http.hpp>

namespace g6::http {

namespace route {
namespace detail {
template <auto pattern, http::method method, typename HandlerT>
struct handler : g6::router::detail::handler<pattern, HandlerT> {
  using base = g6::router::detail::handler<pattern, HandlerT>;

  handler(HandlerT &&handler) noexcept
      : base{std::forward<HandlerT>(handler)} {}

  using base::matches;

  template <typename ContextT, typename ArgsT>
  std::optional<typename base::result_t>
  operator()(ContextT &context, std::string_view path, ArgsT &&args) {
    if (std::get<http::method>(args) == method) {
      return base::operator()(context, path, std::forward<ArgsT>(args));
    } else {
      return {};
    }
  }
};
} // namespace detail

#define XX(num, name, string) \
  template <ctll::fixed_string pattern, typename HandlerT> \
  constexpr auto name(HandlerT &&handler) noexcept { \
    return route::detail::handler<pattern, http::method::name, HandlerT>{ \
        std::forward<HandlerT>(handler)}; \
  }
  G6_HTTP_METHOD_MAP(XX)
#undef XX
} // namespace route

} // namespace g6::http
