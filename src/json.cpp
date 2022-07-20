#include <g6/json/json.hpp>

namespace g6 {
// templates instanciation
template class poly::var<json::boolean, json::number, json::string>;
template class poly::vec<json::boolean, json::number, json::string>;
template class poly::obj<json::boolean, json::number, json::string>;

namespace json {
    template std::string dump<null_t>(null_t const &data);
    template std::string dump<boolean>(boolean const &data);
    template std::string dump<number>(number const &data);
    template std::string dump<string>(string const &data);
    template std::string dump<object>(object const &data);
    template std::string dump<list>(list const &data);
}
}
