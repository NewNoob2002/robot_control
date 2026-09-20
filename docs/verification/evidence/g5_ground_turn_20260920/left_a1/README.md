G5 ground left-turn A1 prepared; same verified51f94c1f artifact. Fresh local START establishes full swept-body/perimeter readiness; prior front/rear clearance is not inherited. Single left steering/throttle neutral, logical(-5,+5) max,3s. Ground rotation/stop observations remain separate from protocol audit. No automatic retry.

## 2026-09-20 地面原地左转 A1 — 有界功能通过

51f94c1f/CLI20/right-sign -1，原始exit0。左轮后退、右轮前进，非零运动2950.011ms，各57个有效方向反馈；零目标后510.090ms完成稳定近零及新帧确认，无重启或反馈违规。2383帧双捕获一致，36次易失写入及完整恢复通过，CAN计数不变；3次missed periods、最大迟到33.105ms保留，无硬实时声明。

现场目测左转约40度，停止后几乎无可见继续运动，中心偏移1–2cm；无X1介入、无围挡接触、中心未越1m边界。OFF确认左轮后退/右轮前进及停止正常、无异响、驱动断电。角度与位移为目测，余转未量化，不声明精确停车转角/机械停转时间，也不从单次估算推导轮距或速度标定。

接受限定无载地面左转与正常停止功能；右转及地面差速仍待对应试验，G5未整体关闭。所有本轮执行器已消耗，无自动重试或新运行。证据：docs/verification/evidence/g5_ground_turn_20260920/left_a1/ground-review.json。
