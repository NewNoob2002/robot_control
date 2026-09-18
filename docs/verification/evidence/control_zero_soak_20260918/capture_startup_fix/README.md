# Capture readiness repair

A1 exposed incompatible candump options and a missing startup guard. The
supervisor now calls phase6_zero_motion_soak.start_capture (the same arguments
used in the accepted3h capture), checks500ms startup survival and stderr before
launching any control process, then retains continuous liveness/lease checks.
The corrected helper also enables all CAN error frames and isolates capture
from terminal HUP. The imported module is shipped with the supervisor.

The inert capture-failure test was red on the prior implementation (application
started too soon), then green after repair; normal finish, failed child and lease
abort still pass. Target smoke was exactly1s receive-only with the drive OFF,
terminated by timeout/SIGINT with rc124 and empty stderr; no CAN send occurred.
This smoke is option/bind validation, not a hardware endurance pass.
