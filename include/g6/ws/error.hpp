#pragma once

#include <system_error>
#include <string>

namespace g6::ws {

    enum class status_code : uint16_t {
        undefined = 0,
        normal_closure = 1000,
        going_away = 1001,
        protocol_error = 1002,
        cannot_accept = 1003,
        // 1004 - reserved
        no_status_code = 1005,
        closed_abnormally = 1006,
        inconsistent_message = 1007,
        policy_violation = 1008,
        too_big = 1009,
        no_extension = 1010,
        unexpected_condition = 1011,
        tls_handshake_failure = 1015,
    };

    inline std::string to_string(status_code status) {
        switch (status) {
            using enum status_code;
            case normal_closure:
                return "normal closure";
            case going_away:
                return "going_away";
            case protocol_error:
                return "protocol_error";
            case cannot_accept:
                return "cannot_accept";
            case no_status_code:
                return "no_status_code";
            case closed_abnormally:
                return "closed abnormally";
            case inconsistent_message:
                return "inconsistent_message";
            case policy_violation:
                return "policy_violation";
            case too_big:
                return "too_big";
            case no_extension:
                return "no_extension";
            case unexpected_condition:
                return "unexpected_condition";
            case tls_handshake_failure:
                return "tls_handshake_failure";
            default:
                return "unknown";
        }
    }

	inline struct error_category_t final : std::error_category {
        [[nodiscard]] const char* name() const noexcept final { return "ws"; }
        [[nodiscard]] std::string message(int error) const noexcept final {
			return to_string(status_code(error));
		}
	} error_category{};

    inline std::error_code make_error_code(status_code status) noexcept {
        return {int(status), ws::error_category};
    }

    inline bool operator==(std::error_code const&error, status_code status) noexcept {
        return error.category() == ws::error_category and error.value() == int(status);
    }
}
