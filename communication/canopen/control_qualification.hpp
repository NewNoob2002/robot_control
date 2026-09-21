#pragma once
#if !defined(ROBOT_CONTROL_CONTROL_HIL) && !defined(ROBOT_CONTROL_TEST_APP)
#error "ControlQualification is available only in the isolated control HIL or test-app artifact"
#endif
#include "communication/canopen/device_parameters.hpp"
#include "communication/canopen/qualification.hpp"
#include "communication/canopen/runtime.hpp"

namespace robot_control::communication::canopen {
/** Bounded HIL bootstrap/cleanup, borrowing the same owner subsequently used by ControlLoop.
 * No enable or nonzero command is issued here. Owner-thread only; stop/destroy the
 * RuntimeSession before finish. Destruction performs no implicit I/O. */
class ControlQualification final {
  public:
    /** Borrow an already opened node-1 owner; it must outlive this noncopyable session. */
    explicit ControlQualification(Lifecycle& owner) noexcept;
    ControlQualification(const ControlQualification&) = delete;
    ControlQualification& operator=(const ControlQualification&) = delete;
    /** Verify exact baseline, establish zero/ready and obtain fresh actual layout readbacks.
     * @param require_quick_stop_option Require current 605A:00=5 before any write for recovery trials.
     * @param zero_tolerance_tenths_rpm Explicit HIL standstill tolerance 0..20; default exact zero.
     * @param speed_resolution Expected preconfigured counts/rpm1..10; read only, zero skips the object for legacy HIL.
     * @param device Optional borrowed expected persistent parameters; read only, lifetime limited to this call.
     * @return Current-owner proof or failure; call finish on every path after prepare.
     * Single owner, once per instance; no persistent parameters or drive enable. */
    [[nodiscard]] platform::linux::Result<RuntimeLayoutProof> prepare(bool require_quick_stop_option = false,
                                                                      int zero_tolerance_tenths_rpm = 0,
                                                                      int speed_resolution = 0,
                                                                      const DeviceConfiguration* device = nullptr);
#ifdef ROBOT_CONTROL_TEST_APP
    /** Configure reviewed persistent parameters/PDO baseline, then request one EEPROM save.
     * @param config Validated value-owned profile; -1 optional fields remain untouched.
     * @return SDO acknowledgement status, not proof of persistence or active scaling.
     * Owner-thread, once per instance; requires disabled/zero state and clean heartbeat/watchdog baseline. No enable,
     * automatic reboot, retry or rollback. Caller must notify the operator to restart. */
    [[nodiscard]] platform::linux::Status configure_device(const DeviceConfiguration& config);
#endif
    /** Zero/disable, verify standstill and restore only owned volatile values once.
     * @return Cleanup status, retaining first failure; cannot run with an attached runtime.
     * Single owner; requires the borrowed owner and original generation to remain alive. */
    [[nodiscard]] platform::linux::Status finish();
    /** Restore only the exact interrupted zero-HIL configuration, without enable or RPDO output.
     * @param zero_tolerance_tenths_rpm Explicit signed feedback tolerance0..20.
     * @return Verified original baseline or failure; foreign/partial layouts reject before writes.
     * Owner-thread only, once per fresh instance; borrows the original live owner throughout.
     */
    [[nodiscard]] platform::linux::Status restore_interrupted(int zero_tolerance_tenths_rpm);

  private:
#ifdef ROBOT_CONTROL_TEST_APP
    /** Read/compare selected persistent parameters; no writes. Owner-thread, borrowed config. */
    [[nodiscard]] platform::linux::Status check_device_parameters(const DeviceConfiguration& config);
    /** Establish or check the fixed node1 app PDO baseline; writes require verified Pre-operational NMT. */
    [[nodiscard]] platform::linux::Status device_pdo_baseline(bool configure);
#endif
    /** Begin one bounded owner generation and validate the initial disabled/ready state. */
    [[nodiscard]] platform::linux::Status begin(int zero_tolerance_tenths_rpm);
    /** Read the exact normal or known interrupted baseline; never writes or claims ownership. */
    [[nodiscard]] platform::linux::Status check_baseline(bool interrupted);
    /** Require fresh SDO speed reads within the explicit tolerance for at least150ms; owner-thread only. */
    [[nodiscard]] platform::linux::Status stable_standstill();
    /** Decode a correlated expedited upload under the original owner generation/deadline. */
    [[nodiscard]] platform::linux::Result<std::uint32_t> read(std::uint16_t index, std::uint8_t sub);
    /** Compare an exact raw value, reporting the object and observed value on mismatch. */
    [[nodiscard]] platform::linux::Status expect(std::uint16_t index, std::uint8_t sub, std::uint32_t value);
    /** Write/read back one width-checked value through the existing qualification gate. */
    [[nodiscard]] platform::linux::Status write(std::uint16_t index, std::uint8_t sub, std::uint32_t value);
    /** Require the initial transport/boot and available phase budget before bus I/O. */
    [[nodiscard]] platform::linux::Status current() const;
    /** Transition NMT with one request and a newer heartbeat confirmation. */
    [[nodiscard]] platform::linux::Status nmt(QualificationNmt command);
    /** Attach fixed node/interface context to a local validation error. */
    [[nodiscard]] platform::linux::Status error(const char* operation, int code) const;
    /** Configure/restore exact RPDO1 or TPDO2 maps only while Pre-operational. */
    [[nodiscard]] platform::linux::Status mapping(bool diagnostics, bool restore);
    Lifecycle& owner_;
    QualificationSession operations_;
    ObservationGeneration generation_{};
    std::chrono::steady_clock::time_point deadline_{};
    bool started_{false}, heartbeat_owned_{false}, watchdog_owned_{false};
    bool rpdo_owned_{false}, diagnostics_owned_{false}, motor_owned_{false}, finished_{false};
    platform::linux::Status finish_status_{};
    int zero_tolerance_{0};
};
} // namespace robot_control::communication::canopen
