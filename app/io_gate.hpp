#pragma once
#include "app/motion_monitor.hpp"
#include "app/record.hpp"

namespace robot_control::app {
/** Explicit caller-owned syscall recording and inhibition context; only the control owner may access it. */
struct IoContext {
    RecordWriter& records;
    MotionMonitor monitor;
    int max_rpm{5};
    bool closed{false};
    int speed_resolution{1};
};
/** Bind a borrowed context for the GNU syscall wrappers; all lifecycle I/O must be inside this lifetime. */
class IoBinding final {
  public:
    /** Attach on the sole control thread before opening CAN; value must outlive the binding. */
    explicit IoBinding(IoContext& value) noexcept;
    /** Detach after all CAN cleanup; destruction sends nothing. */
    ~IoBinding();
    IoBinding(const IoBinding&) = delete;
    IoBinding& operator=(const IoBinding&) = delete;
};
} // namespace robot_control::app
