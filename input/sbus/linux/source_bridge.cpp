#include "input/sbus/linux/source_bridge.hpp"

namespace robot_control::input::sbus {
SourceSnapshot update_source(Source& source, const platform::linux::Result<ReadBatch>& result, MonotonicTime now) {
    if (!result.ok())
        return source.transport_error(now);
    const auto& batch = result.value();
    if (batch.event_count > batch.events.size() || batch.raw_size > batch.raw.size())
        return source.transport_error(now);
    return source.consume({std::span{batch.events}.first(batch.event_count), batch.captured_at, batch.session,
                           batch.discontinuity != Discontinuity::none},
                          now);
}
} // namespace robot_control::input::sbus
