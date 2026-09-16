#pragma once

#include "input/sbus/linux/reader.hpp"
#include "input/sbus/source/source.hpp"

namespace robot_control::input::sbus {
/**
 * Deliver a bounded reader result to the pure source, including silence/errors.
 * @param source Borrowed single-owner command producer.
 * @param result Borrowed ordered UART result; spans are not retained.
 * @param now Injected monotonic processing time from the reader clock domain.
 * @return Owned coherent snapshot. Device error details remain in result.status().
 * Thread safety: Called only by the source producer; snapshot readers may run.
 * No device access, reconnect or drive authority is introduced by this bridge.
 */
[[nodiscard]] SourceSnapshot update_source(Source& source, const platform::linux::Result<ReadBatch>& result,
                                           MonotonicTime now);
} // namespace robot_control::input::sbus
