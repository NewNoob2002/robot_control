#include "platform/linux/can/socket.hpp"
#include "platform/linux/process/termination_event.hpp"

#include <linux/can/error.h>

#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace {

namespace linux_can = robot_control::platform::linux::can;
namespace linux_process = robot_control::platform::linux::process;
using robot_control::platform::linux::Result;
using robot_control::platform::linux::Status;

constexpr std::chrono::milliseconds kDefaultDuration{1000};
constexpr std::chrono::milliseconds kMaximumDuration{60000};

struct Options {
  std::string interface_name;
  std::chrono::milliseconds duration{kDefaultDuration};
};

/**
 * Print command-line usage without opening a SocketCAN socket.
 *
 * Thread safety: Standard-output writes require external synchronization.
 */
void print_usage() {
  std::cout << "Usage: robot-control-can-probe --interface <ifname> "
               "[--duration-ms <milliseconds>]\n"
            << "       robot-control-can-probe --help\n"
            << "duration-ms must be in the range 1.."
            << kMaximumDuration.count() << ".\n";
}

/**
 * Write one quoted, single-line-safe field value.
 *
 * @param output Stream that receives the escaped value.
 * @param value Value borrowed for the duration of this call.
 *
 * Thread safety: The caller must serialize writes to output.
 */
void print_quoted(std::ostream &output, const std::string_view value) {
  output.put('"');
  for (const char character : value) {
    switch (character) {
    case '\\':
      output << "\\\\";
      break;
    case '"':
      output << "\\\"";
      break;
    case '\n':
      output << "\\n";
      break;
    case '\r':
      output << "\\r";
      break;
    case '\t':
      output << "\\t";
      break;
    default:
      output.put(character);
      break;
    }
  }
  output.put('"');
}

/**
 * Print one context-rich error record.
 *
 * @param status Failure status borrowed for this call.
 *
 * Thread safety: Standard-error writes require external synchronization.
 */
void print_error(const Status &status) {
  std::cerr << "event=error operation=";
  print_quoted(std::cerr, status.operation);
  std::cerr << " context=";
  print_quoted(std::cerr, status.context);
  std::cerr << " errno=" << status.error.value() << " message=";
  print_quoted(std::cerr, status.error.message());
  std::cerr << '\n';
}

/**
 * Create a command-line parsing failure.
 *
 * @param context Diagnostic argument context transferred into the result.
 * @param error_number POSIX error number describing the invalid input.
 * @return Failed options result with stable parse operation identity.
 *
 * Thread safety: Pure and reentrant.
 */
Result<Options> argument_error(std::string context, const int error_number) {
  return Result<Options>::failure(
      Status::from_errno("parse_arguments", std::move(context), error_number));
}

/**
 * Parse the probe command line after the standalone help case is handled.
 *
 * @param argc Number of command-line arguments.
 * @param argv Argument vector owned by the process runtime.
 * @return Validated options or a context-rich parsing failure.
 *
 * Thread safety: Pure and reentrant for independent argument vectors.
 */
Result<Options> parse_options(const int argc, char *argv[]) {
  Options options;
  bool interface_seen = false;
  bool duration_seen = false;

  for (int index = 1; index < argc; ++index) {
    const std::string_view argument{argv[index]};
    if (argument == "--interface") {
      if (interface_seen) {
        return argument_error("duplicate argument=--interface", EINVAL);
      }
      if (index + 1 >= argc ||
          std::string_view{argv[index + 1]}.starts_with("--")) {
        return argument_error("missing value for --interface", EINVAL);
      }
      options.interface_name = argv[++index];
      if (options.interface_name.empty()) {
        return argument_error("empty value for --interface", EINVAL);
      }
      interface_seen = true;
      continue;
    }

    if (argument == "--duration-ms") {
      if (duration_seen) {
        return argument_error("duplicate argument=--duration-ms", EINVAL);
      }
      if (index + 1 >= argc ||
          std::string_view{argv[index + 1]}.starts_with("--")) {
        return argument_error("missing value for --duration-ms", EINVAL);
      }
      const std::string_view value{argv[++index]};
      std::uint64_t milliseconds = 0;
      const auto parsed = std::from_chars(
          value.data(), value.data() + value.size(), milliseconds);
      if (parsed.ec == std::errc::result_out_of_range) {
        return argument_error(
            "duration-ms overflow value=" + std::string{value}, ERANGE);
      }
      if (parsed.ec != std::errc{} ||
          parsed.ptr != value.data() + value.size()) {
        return argument_error("invalid duration-ms value=" + std::string{value},
                              EINVAL);
      }
      if (milliseconds == 0 ||
          milliseconds > static_cast<std::uint64_t>(kMaximumDuration.count())) {
        return argument_error(
            "duration-ms out of range value=" + std::string{value}, ERANGE);
      }
      options.duration =
          std::chrono::milliseconds{static_cast<std::int64_t>(milliseconds)};
      duration_seen = true;
      continue;
    }

    return argument_error("unknown argument=" + std::string{argument}, EINVAL);
  }

  if (!interface_seen) {
    return argument_error("missing required argument=--interface", EINVAL);
  }
  return Result<Options>::success(std::move(options));
}

/**
 * Format a raw CAN identifier as eight lowercase hexadecimal digits.
 *
 * @param raw_can_id Complete Linux CAN identifier including all flag bits.
 * @return Owned, fixed-width hexadecimal representation.
 *
 * Thread safety: Pure and reentrant.
 */
std::string format_can_id(const std::uint32_t raw_can_id) {
  std::ostringstream output;
  output << "0x" << std::hex << std::nouppercase << std::setfill('0')
         << std::setw(8) << raw_can_id;
  return output.str();
}

/**
 * Format the active payload bytes as contiguous lowercase hexadecimal.
 *
 * @param frame Complete frame borrowed for this call.
 * @return Owned hexadecimal representation of payload_length bytes.
 *
 * Thread safety: Pure and reentrant.
 */
std::string format_payload(const linux_can::ClassicCanFrame &frame) {
  std::ostringstream output;
  output << std::hex << std::nouppercase << std::setfill('0');
  for (std::size_t index = 0; index < frame.payload_length; ++index) {
    output << std::setw(2) << std::to_integer<unsigned int>(frame.data[index]);
  }
  return output.str();
}

/**
 * Print one complete received-frame observation.
 *
 * Kernel timestamps remain raw realtime-domain values for diagnostics, and
 * overflow counters remain raw cumulative values. Missing metadata is printed
 * explicitly and never synthesized.
 *
 * @param sequence One-based sequence number for this process invocation.
 * @param observation Complete observation borrowed for this call.
 *
 * Thread safety: Standard-output writes require external synchronization.
 */
void print_frame(const std::uint64_t sequence,
                 const linux_can::CanReceiveObservation &observation) {
  const auto &frame = observation.frame;
  std::cout << "event=frame sequence=" << sequence
            << " raw_can_id=" << format_can_id(frame.raw_can_id)
            << " payload_length="
            << static_cast<unsigned int>(frame.payload_length)
            << " len8_dlc=" << static_cast<unsigned int>(frame.len8_dlc)
            << " data=" << format_payload(frame);
  if (observation.kernel_timestamp.has_value()) {
    std::cout << " kernel_timestamp_sec="
              << observation.kernel_timestamp->tv_sec
              << " kernel_timestamp_nsec="
              << observation.kernel_timestamp->tv_nsec;
  } else {
    std::cout << " kernel_timestamp_sec=none kernel_timestamp_nsec=none";
  }
  if (observation.rx_queue_overflow.has_value()) {
    std::cout << " rx_queue_overflow=" << *observation.rx_queue_overflow;
  } else {
    std::cout << " rx_queue_overflow=none";
  }
  std::cout << '\n';
}

/**
 * Print the terminal probe summary.
 *
 * @param reason Stable terminal reason token.
 * @param frames Number of complete frame observations printed.
 * @param signal Consumed signal number for signal termination, if any.
 *
 * Thread safety: Standard-output writes require external synchronization.
 */
void print_summary(const std::string_view reason, const std::uint64_t frames,
                   const std::optional<int> signal = std::nullopt) {
  std::cout << "event=summary reason=" << reason;
  if (signal.has_value()) {
    std::cout << " signal=" << *signal;
  }
  std::cout << " frames=" << frames << '\n';
}

/**
 * Run the passive probe until its fixed monotonic deadline or cancellation.
 *
 * @param options Validated command-line options borrowed for this call.
 * @return Zero on deadline, 128 plus signal on SIGINT/SIGTERM, or one on a
 * runtime failure.
 *
 * Thread safety: Single-threaded; the function owns its socket and signal
 * event and performs unsynchronized standard-stream writes.
 */
int run_probe(const Options &options) {
  const auto deadline = std::chrono::steady_clock::now() + options.duration;

  auto termination = linux_process::TerminationEvent::create();
  if (!termination.ok()) {
    print_error(termination.status());
    return 1;
  }

  linux_can::CanSocketConfig config{};
  config.error_mask = CAN_ERR_MASK;
  config.receive_timestamp = true;
  config.receive_queue_overflow = true;
  auto socket = linux_can::CanSocket::open(options.interface_name, config);
  if (!socket.ok()) {
    print_error(socket.status());
    return 1;
  }

  std::cout << "event=start probe_version=1 interface=";
  print_quoted(std::cout, socket.value().interface_name());
  std::cout << " interface_index=" << socket.value().interface_index()
            << " duration_ms=" << options.duration.count() << '\n';

  std::uint64_t frames = 0;
  while (true) {
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline) {
      print_summary("deadline", frames);
      return 0;
    }
    const auto remaining =
        std::chrono::ceil<std::chrono::milliseconds>(deadline - now);
    auto received = socket.value().receive(remaining, termination.value().fd());
    if (!received.ok()) {
      if (received.status().error.value() != ECANCELED) {
        print_error(received.status());
        return 1;
      }
      auto signal = termination.value().consume();
      if (!signal.ok()) {
        print_error(signal.status());
        return 1;
      }
      if (signal.value() == 0) {
        continue;
      }
      print_summary("signal", frames, signal.value());
      return 128 + signal.value();
    }
    auto observation = std::move(received).value();
    if (!observation.has_value()) {
      print_summary("deadline", frames);
      return 0;
    }
    ++frames;
    print_frame(frames, observation.value());
  }
}

} // namespace

/**
 * Run the bounded, receive-only SocketCAN probe.
 *
 * @param argc Number of command-line arguments.
 * @param argv Argument vector owned by the process runtime.
 * @return Zero on help or deadline, two for invalid arguments, one for runtime
 * failures, or 128 plus signal after consuming SIGINT/SIGTERM.
 */
int main(const int argc, char *argv[]) {
  if (argc == 2 && std::string_view{argv[1]} == "--help") {
    print_usage();
    return 0;
  }

  auto options = parse_options(argc, argv);
  if (!options.ok()) {
    print_error(options.status());
    print_usage();
    return 2;
  }
  return run_probe(options.value());
}
