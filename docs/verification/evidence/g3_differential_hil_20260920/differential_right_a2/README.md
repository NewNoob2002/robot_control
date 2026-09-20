## 2026-09-20 右差速 A2 功能验收 PASS

用户确认先推满前进油门再右转。51f94c1f/CLI20/right-sign -1，运动1460.002ms，7个有效右慢左快反馈样本；后续CH1=1357/CH3=1800产生(5,0)候选并被发送前拒绝。按用户批准的差速功能准则接受已完成的差速证据，原始exit1/primary_ok0不变。稳定停止300.536ms，无反馈违规/重启；1121帧双采集一致，36次易失写入完整恢复，CAN计数不变；现场OFF确认双轮前进/停止正常、无异响、驱动断电。左右差速功能项现已接受；无新软件修改、无新硬件运行授权，不扩展到负载/落地/生产。证据：docs/verification/evidence/g3_differential_hil_20260920/differential_right_a2/revised-acceptance.json。
