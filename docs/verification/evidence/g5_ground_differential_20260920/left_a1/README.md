G5 ground left differential A1 prepared. Artifact51f94c1f/CLI20/right-sign-1. Forward throttle first then left steering, no prescribed percentage. Nonzero<3s includes equal approach; <=5rpm. At least2 independent ordered feedback samples required. Raw protective failures preserved under approved functional review; physical ground path/stop observations remain separate. New local START mandatory, no automatic retry.

## 2026-09-20 地面左差速 A1 手推干扰与恢复缺口

用户报告意外用手推车并授权一次重测。原试次在运动准备完成之前退出，enabled_samples=0、反馈门失败；清理静止反馈检查失败，restore_ok=0/control_hil_cleanup_unverified/operator_power_off_required。不能将此情况按普通已恢复试次直接重跑。原失败和所有数据保留；用户重测授权保留，先确认实际停稳/机械防溜/驱动断电，再核对当前参数与原始基线并准备有界恢复。不把用户手推说明当作已证明唯一故障原因。本轮没有新增恢复写入或重测。
