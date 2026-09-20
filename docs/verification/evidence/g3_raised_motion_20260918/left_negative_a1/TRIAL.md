# G3 left-negative A1 — frozen trial, local START pending

Artifact5f44146c; machine6923ab3301fb4a8d816759b04ec6bf0a, node1/can0 at500k,
UART586D017868, JCAN207F346D5650. Zero A2 prerequisite PASS.
Exact command, scope and recovery are recorded in authorization.json and
safety-preflight.json. Single-left-negative mode only, left-5..-1rpm/right0,
60s total preparation window,3s hard movement limit/2950ms cutoff.

Visible console: START confirms actual drive power, raised/unloaded wheels,
neutral controls, CH6 released, X1 released/accessible and unchanged wiring.
CONTROL_READY only after healthy neutral Disabled input. Press/release CH6
once; wait for20 zero enabled cycles and MOTION_READY. Move throttle backward
and steering left together, maintaining the diagonal towards CH1=200/CH3=200;
do not first move one axis to its endpoint.801 ideal coordinated points pass
the existing mapping offline; this does not prove manual usability. Profile
mismatch protects/stops without a retry. Hold until STOP or wheel stop,
immediately return neutral. No intentional fault stimulus. Actual wheel
reverse/stop and opposite-wheel stillness require independent operator feedback.

Left negative feedback must exceed2rpm magnitude in two fresh frames;
right always within+/-2rpm. Within1s of zero, dual-axis150ms stable band plus
another fresh frame required. Final exact zero/Disable Voltage,36 bounded
volatile writes/full current baseline restoration and exact dual capture
required. Application failure is not motion acceptance. Lock X1 and drive OFF
after exit; preserve all failure evidence and consume both runners.

Offline check: python3 -B .../left_negative_a1/check_runner.py; capture self-check.
Current123 source inputs and artifact hash match the accepted software baseline.
No application source edits or new build are part of this trial.
