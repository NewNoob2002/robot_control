#include "communication/canopen/stack_config.hpp"

#include <linux/if.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>

namespace robot_control::communication::canopen {

platform::linux::Status
validate_stack_config(const StackConfig &config) noexcept {
  const auto invalid = [](const char *context, const int error_number = EINVAL) {
    return platform::linux::Status::from_errno("validate_canopen_stack_config",
                                               context, error_number);
  };

  if (config.controller_node_id < 1U || config.controller_node_id > 127U) {
    return invalid("controller_node_id");
  }
  if (config.remote_node_id < 1U || config.remote_node_id > 127U) {
    return invalid("remote_node_id");
  }
  if (config.controller_node_id == config.remote_node_id) {
    return invalid("node_ids_must_differ");
  }
  if (config.interface_name.empty()) {
    return invalid("interface_name");
  }
  if (config.interface_name.size() >= IFNAMSIZ) {
    return invalid("interface_name", ENAMETOOLONG);
  }
  if (config.interface_name == "." || config.interface_name == ".." ||
      std::any_of(config.interface_name.begin(), config.interface_name.end(),
                  [](const unsigned char character) {
                    return character == '/' || std::isspace(character) != 0 ||
                           std::iscntrl(character) != 0;
                  })) {
    return invalid("interface_name");
  }

  constexpr std::array<std::uint16_t, 5> supported_bit_rates{100U, 125U, 250U,
                                                             500U, 1000U};
  if (std::ranges::find(supported_bit_rates, config.bit_rate_kbit_s) ==
      supported_bit_rates.end()) {
    return invalid("bit_rate_kbit_s");
  }

  constexpr auto maximum_timeout = std::chrono::milliseconds{65535};
  if (config.heartbeat_timeout <= std::chrono::milliseconds::zero() ||
      config.heartbeat_timeout > maximum_timeout) {
    return invalid("heartbeat_timeout");
  }
  if (config.sdo_timeout <= std::chrono::milliseconds::zero() ||
      config.sdo_timeout > maximum_timeout) {
    return invalid("sdo_timeout");
  }
  return platform::linux::Status::success();
}

} // namespace robot_control::communication::canopen
