#pragma once

#include <map>
#include <string>

namespace g6::http
{
namespace detail {
  #include <http_parser.h>
}

/* Status Codes */
#define G6_HTTP_STATUS_MAP(XX)                                                 \
  XX(100, continue_,                       Continue)                        \
  XX(101, switching_protocols,             Switching Protocols)             \
  XX(102, processing,                      Processing)                      \
  XX(200, ok,                              OK)                              \
  XX(201, created,                         Created)                         \
  XX(202, accepted,                        Accepted)                        \
  XX(203, non_authoritative_information,   Non-Authoritative Information)   \
  XX(204, no_content,                      No Content)                      \
  XX(205, reset_content,                   Reset Content)                   \
  XX(206, partial_content,                 Partial Content)                 \
  XX(207, multi_status,                    Multi-Status)                    \
  XX(208, already_reported,                Already Reported)                \
  XX(226, im_used,                         IM Used)                         \
  XX(300, multiple_choices,                Multiple Choices)                \
  XX(301, moved_permanently,               Moved Permanently)               \
  XX(302, found,                           Found)                           \
  XX(303, see_other,                       See Other)                       \
  XX(304, not_modified,                    Not Modified)                    \
  XX(305, use_proxy,                       Use Proxy)                       \
  XX(307, temporary_redirect,              Temporary Redirect)              \
  XX(308, permanent_redirect,              Permanent Redirect)              \
  XX(400, bad_request,                     Bad Request)                     \
  XX(401, unauthorized,                    Unauthorized)                    \
  XX(402, payment_required,                Payment Required)                \
  XX(403, forbidden,                       Forbidden)                       \
  XX(404, not_found,                       Not Found)                       \
  XX(405, method_not_allowed,              Method Not Allowed)              \
  XX(406, not_acceptable,                  Not Acceptable)                  \
  XX(407, proxy_authentication_required,   Proxy Authentication Required)   \
  XX(408, request_timeout,                 Request Timeout)                 \
  XX(409, conflict,                        Conflict)                        \
  XX(410, gone,                            Gone)                            \
  XX(411, length_reguired,                 Length Required)                 \
  XX(412, precondition_failed,             Precondition Failed)             \
  XX(413, payload_too_large,               Payload Too Large)               \
  XX(414, uri_too_long,                    URI Too Long)                    \
  XX(415, unsupported_media_type,          Unsupported Media Type)          \
  XX(416, range_not_satisfiable,           Range Not Satisfiable)           \
  XX(417, expectation_failed,              Expectation Failed)              \
  XX(421, misdirected_request,             Misdirected Request)             \
  XX(422, unprocessable_entity,            Unprocessable Entity)            \
  XX(423, locked,                          Locked)                          \
  XX(424, failed_dependency,               Failed Dependency)               \
  XX(426, updrage_required,                Upgrade Required)                \
  XX(428, precondition_required,           Precondition Required)           \
  XX(429, too_many_requests,               Too Many Requests)               \
  XX(431, request_header_fields_too_large, Request Header Fields Too Large) \
  XX(451, unavailable_for_legal_reasons,   Unavailable For Legal Reasons)   \
  XX(500, internal_server_error,           Internal Server Error)           \
  XX(501, not_implemented,                 Not Implemented)                 \
  XX(502, bad_gateway,                     Bad Gateway)                     \
  XX(503, service_unavailable,             Service Unavailable)             \
  XX(504, gateway_timeout,                 Gateway Timeout)                 \
  XX(505, http_version_not_supported,      HTTP Version Not Supported)      \
  XX(506, variant_alse_negotiates,         Variant Also Negotiates)         \
  XX(507, insufficient_storage,            Insufficient Storage)            \
  XX(508, loop_detected,                   Loop Detected)                   \
  XX(510, not_extended,                    Not Extended)                    \
  XX(511, network_authentication_required, Network Authentication Required) \

enum class status
{
#define XX(num, name, string) name = num,
  G6_HTTP_STATUS_MAP(XX)
#undef XX
};

/* Request Methods */
#define G6_HTTP_METHOD_MAP(XX)         \
  XX(0,  delete_,     DELETE)       \
  XX(1,  get,         GET)          \
  XX(2,  head,        HEAD)         \
  XX(3,  post,        POST)         \
  XX(4,  put,         PUT)          \
  /* pathological */                \
  XX(5,  connect,     CONNECT)      \
  XX(6,  options,     OPTIONS)      \
  XX(7,  trace,       TRACE)        \
  /* WebDAV */                      \
  XX(8,  copy,        COPY)         \
  XX(9,  lock,        LOCK)         \
  XX(10, mkcol,       MKCOL)        \
  XX(11, move,        MOVE)         \
  XX(12, propfind,    PROPFIND)     \
  XX(13, proppatch,   PROPPATCH)    \
  XX(14, search,      SEARCH)       \
  XX(15, unlock,      UNLOCK)       \
  XX(16, bind,        BIND)         \
  XX(17, rebind,      REBIND)       \
  XX(18, unbind,      UNBIND)       \
  XX(19, acl,         ACL)          \
  /* subversion */                  \
  XX(20, report,      REPORT)       \
  XX(21, mkactivity,  MKACTIVITY)   \
  XX(22, checkout,    CHECKOUT)     \
  XX(23, merge,       MERGE)        \
  /* upnp */                        \
  XX(24, msearch,     M-SEARCH)     \
  XX(25, notify,      NOTIFY)       \
  XX(26, subscribe,   SUBSCRIBE)    \
  XX(27, unsubscribe, UNSUBSCRIBE)  \
  /* RFC-5789 */                    \
  XX(28, patch,       PATCH)        \
  XX(29, purge,       PURGE)        \
  /* CalDAV */                      \
  XX(30, mkcalendar,  MKCALENDAR)   \
  /* RFC-2068, section 19.6.1.2 */  \
  XX(31, link,        LINK)         \
  XX(32, unlink,      UNLINK)       \
  /* icecast */                     \
  XX(33, source,      SOURCE)       \

enum class method
{
#define XX(num, name, string) name = num,
  G6_HTTP_METHOD_MAP(XX)
#undef XX
};

using headers = std::multimap<std::string, std::string>;

}