G5 ground right-turn A1 prepared from completed left-turn flow. Artifact51f94c1f, right-sign-1, CLI20, abs<=5rpm/<3s. Right steering only/throttle neutral: logical(+5,-5). Fresh local START confirms complete swept-body/perimeter readiness. Ground yaw and stopping observations independently reviewed; no automatic retry.

## 2026-09-20 地面原地右转 A1 — 有界功能通过

51f94c1f/CLI20/right-sign -1，原始exit0。左轮前进、右轮后退，非零运动2950.011ms，各57个有效方向反馈；零目标后470.097ms完成稳定近零及新帧确认，无重启或反馈违规。1695帧双捕获一致，36次易失写入及完整恢复通过，CAN计数不变；4次missed periods、最大迟到43.626ms保留，不声明硬实时。

现场目测右转约45度，停止后余转几乎没有，中心偏移约1cm；记录无X1介入、未接触围挡、中心未越1m边界。OFF确认左轮前进/右轮后退及停止正常、无异响、驱动断电。角度和位移为目测，余转未量化，不声明精确机械停车角度/时间，不以左右40/45度估计推断系统偏差。

接受限定无载地面右转与正常停止功能。地面前进、后退、原地左右转向分项已有通过证据，地面左右差速仍待独立测试，G5未整体关闭。所有本轮执行器已消耗，不自动重试或启动下一项。证据：docs/verification/evidence/g5_ground_turn_20260920/right_a1/ground-review.json。
