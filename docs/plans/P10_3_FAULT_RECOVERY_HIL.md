# P10.3 整链故障与恢复实机工况 — 2026-09-17

用户已授权开始完成整链 X1/急停、quick-stop 恢复、SBUS 失联/failsafe、非中立恢复和运动 SIGTERM。
本授权不覆盖旧执行器重用、扩大速度/时长、负向/双轮运动、CAN 丢失、持久参数或内核变更。
左右轮停止后负反馈原始失败、抓包、哈希和操作员断电确认全部保留，不改变旧判据。
最新已确认驱动 OFF。本轮尚未开始物理刺激；每项只执行一次，先准备和验证，再确认当前现场就绪。

## 工况顺序与证据

| 工况 | 刺激与范围 | 必须证明 |
| --- | --- | --- |
| F1 零目标 X1 恢复 | 初始中立新 CH6 零使能；按锁现有 X1 至应用确认保持1秒；解除后非中立 CH6 挑战，再回中、新授权 | X1低半字bit15逐事件撤权；解除不自启；非中立边沿不留下待执行权限；quick-stop后先Disable Voltage、新Disabled反馈，再Shutdown/SwitchOn/Enable；全过程目标与速度零 |
| F2 零目标 SBUS 恢复 | 中立使能后关闭发射机；应用依据真实frame_lost/failsafe/timeout记录刺激，恢复连接后非中立 CH6 挑战，再回中、新授权 | 实际Source撤权；flags恢复或新帧不恢复旧权限；新中立授权才能重新零使能；不得把RF失联自动记为UART静默 |
| F3 运动 X1 | 独立右轮正向≤5rpm/<3秒；应用MOTION_READY后运动，现场立即按锁X1 | 原始X1位、最后非零/首次零、逐事件禁止后续非零、稳定停车；保留停止反馈异常；锁定X1至停机/断电，不自动重启 |
| F4 运动 SBUS loss/failsafe | 同样右轮窗口，现场关闭发射机；必须在自动截止前捕获实际失联证据 | 区分真实失联撤权与3秒兜底停止；flags4/12或实际timeout分别报告；无后续非零；零速与清理 |
| F5 真实UART静默（条件项） | 需先明确现场可安全执行的接收端静默方法；不根据RF失联假设串口已静默 | 用实际接收间隔证明timeout；USB拔除是transport错误，须单列，不能冒充可恢复静默 |
| F6 运动SIGTERM | 目标runner观察到右轮正反馈后约300ms，向自己启动且核验的应用PID发送一次SIGTERM；不由聊天计时 | 输入撤销、信号处理和零RPDO；内部报告≤100ms；1秒内稳定零，配置恢复和进程退出；不把kill/外部切CAN当作正常停机 |

先审查F1，再F2；其余工况分别准备执行器和当前现场确认，不自动串联。已观察到的负反馈如再次出现，
仍记录原负反馈判据失败；可单列“故障被识别/撤权/稳定零/恢复配置”的证据，不能冒充整项通过。
刺激未在非零窗口内发生则本工况不成立，不以自动截止替代故障停止证据，不自动重试。

## 零目标恢复工具

新增Debug/default-OFF CLI：--zero-x1-recovery、--zero-sbus-recovery，运行窗口2..60秒。
仍使用真实Reader/Source、ControlLoop、RuntimeSession和同一Lifecycle；只增加试验观察状态，
不覆盖样本、不制造授权、不注入生产安全条件。独立send门拒绝所有非零SDO/RPDO，非中立候选仍原样记录。
进入恢复试验前读取并要求605A:00=5，任何不符在临时写入前拒绝；不写此对象。

应用阶段：CONTROL_READY → fault_ready → fault_observed → release_fault → neutral_ready → rearm_ready
→ zero_reenabled → complete。所有阶段输出单调时刻和源/系统授权代际。

- CONTROL_READY：摇杆中立，CH6按一次释放，等待fault_ready。
- fault_ready：F1按锁X1；F2关闭发射机。不要提前解除。
- release_fault：应用已看到撤权且刺激保持至少1秒。F1解除X1；F2开启发射机。
  油门向前保持非中立，CH6按一次后释放，等待neutral_ready。全过程最终目标被独立门限制为零。
- neutral_ready：摇杆回中、CH6释放，保持至少1秒；应用检查不得自启。
- rearm_ready：中立下CH6再按一次释放。必须出现新源授权及新系统授权，按实测状态恢复零使能。
- complete：新零使能保持至少1秒，随后显式停止、解除runtime并恢复临时配置。

窗口内未完整完成、额外CAN故障/诊断异常、非零反馈、证据溢出、未经新授权的使能或恢复链不完整均失败。
X1原有逐事件抑制可能先输出零Shutdown6；该帧不是重新授权。禁止旧授权的SwitchOn7/Enable15，
并独立验证新授权后的QuickStopActive→Disable Voltage→Disabled→Shutdown顺序。

## 设备、边界和收尾

沿用robot-dev、machine6923ab3301fb4a8d816759b04ec6bf0a、can0/node1 ZLAC8015D、Classical CAN500000、
UART586D017868及JCAN207F346D5650。X1沿用既有INPUT2按锁/旋转复位夹具，不写输入配置。
双轮架空卸载、无人触碰、现场操作员可立即急停/断电；接线及原电气未测项不变。

JCAN silent先就绪，随后目标candump，再RK3588应用；JCAN/Python不发送控制序列。
临时watchdog1000ms、setup/cleanup各10秒、运行60秒、外层90秒、退出清理宽限12秒、
抓包120秒/20000帧、trace65536条/退出后5秒导出。移动工况仍≤5rpm、首次零永久关闭非零窗口、
2.95秒主动截止、3秒硬拒绝；1秒内连续零反馈150ms并再收到一帧，不改变负反馈容差。

全程保留双路原始帧、完整trace、错误和恢复结果。每种时间轴分别计算；JCAN若仍全零时间戳，
只能证明字节/顺序一致，不宣称独立时延。任何失败仍取回证据，保留清理失败；必要时现场断电。
逐项确认轮别/方向、停止、声音和最终OFF。P10.3、P6长稳及delayed-TX问题保持各自状态。

## A3 零目标容差修订（用户明确指定 ±1rpm）

A1/A2 原始失败保留。后续零目标资格验证可显式使用
--zero-feedback-tenths-rpm 10，默认仍为0；不允许用于运动或只读观察模式。
初始读回、双轴逐帧观察、运行时静止判断、恢复观察与退出清理采用同一有符号范围。
前置及退出 SDO 606C:01/02/03 连续采样在范围内至少150ms，越界失败。
所有发送目标仍严格为0，原始反馈不改写；不改生产策略或旧运动反馈判据。
A2 恢复未完成，后续运行须完整读回基线。静止目视确认支持容差选择，不证明传感器根因。

### 单轮运动验收收口：CLOSED；反馈判据修订为 ±1.5rpm

用户确认停止时负反馈来自驱动器内部速度估算，按正常波动记录，并明确关闭该保留项。
左右轮方向、实际停止、对侧静止及无异响已有现场确认；两轮 A1 原始轨迹按±1.5rpm复核通过，
见 standstill_15_revision/single-wheel-revised-audit.json。旧抓包及原判据失败保留，新增修订结论；
历史−3.6rpm样本不属于新阈值数值范围，不将其重写为±1.5rpm内。
后续 HIL 统一使用 --zero-feedback-tenths-rpm 15（控制模式默认15），包含运动/停车与清理。
原始速度不改写，非零目标、单轮限速/3秒上限不变，明显反向超阈值仍拒绝；>=150ms稳定观察保留。
F3运动中X1时序仍须独立通过，不能由单轮收口或容差替代。当前新F3 A3准备重跑。

### F4 A4：用户授权扩大运动窗口，适配长按关机（2026-09-17）

A1误按X1、A2未复位X1、A3自动截止且未观察到SBUS失联均保留为INCOMPLETE。
用户确认关机需约1–2秒长按且没有屏幕提示，明确允许增大窗口后重新测试。
A4显式使用 --motion-window-ms8000；应用7950ms主动截止，独立发送门8000ms硬拒绝。
默认仍3000ms，其他历史试验不变；右轮≤5rpm、左轮目标0、反馈±1.5rpm、无自动重启。
MOTION_READY后推油门；目标执行器观察到右轮正反馈超过1秒再给POWER_OFF_READY，
现场长按遥控器电源键至关机，然后回中；接收机持续供电。该提示不产生任何控制命令。
真实失联必须早于首次零目标和7950ms截止，仍要求≤100ms撤权到零、1秒内稳定停车、
双抓包一致与完整临时配置恢复；8秒兜底停车不能代替F4通过。
新产物、新执行器和新现场确认仅授权一次A4；不扩大到其他故障或自动重试。


## Explicit ±2rpm revision (2026-09-17)

User approved raising the startup/stop feedback band to±2rpm. Control HIL CLI
--zero-feedback-tenths-rpm now accepts0..20 and defaults20; library defaults
remain0. Historical analysis defaults and recorded15 criteria are retained;
revised analysis must explicitly pass20. Raw feedback and original failures
are never rewritten. Positive motion limits, zero-command requirements,
>=150ms stable holds and fault timing/rearm rules remain unchanged.
F4 A5 is accepted by retrospective analysis under20, while its original15
failure remains preserved. No new physical test occurred for this revision.
See standstill_20_revision/README.md in the fault-recovery evidence directory.


## F5 A2 — signal-only disconnect with partial-frame handling

User confirms a separately removable SBUS signal connector and authorizes A2.
Use zero-target --zero-uart-recovery/CLI20 for60s, not a moving trial. Keep USB,
power,ground and transmitter ON; restore a healthy signal before current START.
An unplug may interrupt a frame. Permit one empty partial-timeout batch as the
fault boundary while retaining immediate Source revocation/session change; verify
>=1s raw-byte-free hold and then nonneutral rejection/neutral/newCH6 recovery.
Reject unrelated discontinuities and transport failures; no automatic retry.
A1 original failure remains unchanged. No voltage-level silence claim is made.


## F5 timing revision after A2

User reports disconnect/reconnect interval and window too short. Future F5 uses
minimum5s disconnect hold,45s reconnection/nonneutral-challenge allowance and120s
total. Initial reconnect candidates may be rejected while authority remains
revoked; wait for INPUT_RESTORED before throttle/CH6. Reader/Source revocation
and fresh rearm are unchanged. Outer guards150s/170s,capture180s/40000frames.
A2 exited on source_fault2 after reconnect, not window exhaustion. Preserve its
incomplete record and consumed authorization; offline A3 preparation is not a
new physical permit. No extension applies to motor movement or F2/F4.


## F5 A4 continuous stability revision

User authorizes optimization and one A4. During phase2's existing45s window,
record repeated parser rejections, retain zero/revoked authority and restart stable
qualification. Require1s fresh healthy neutral input/CH6 released before prompting
the nonneutral challenge. If input degrades again, invalidate prompt and challenge
progress; do not queue CH6 edges or extend deadline. Full fresh neutral rearm and
all hard guards remain required. Preserve A3's rejected-after-health original result.


## F6 A1 acceptance and planned matrix closure (2026-09-17)

F6 A1 passes one right<=5rpm/<3s moving SIGTERM trial with91d3ce99/CLI20.
Signal after312.680ms observed positive feedback,zero-send upper bound312.960us,
stable stop253.266ms,exit770.230ms;1507 dual frames match/full volatile restoration.
Operator confirms OFF/direction+stop normal/left stationary/no sound. Both markers
consumed. F1–F6 bounded unloaded HIL matrix is accepted; see
../verification/P10_3_HIL_CHECKPOINT.md and motion_sigterm_a1/acceptance.json.
Historical failed/incomplete trials and pending A6 feedback review remain intact;
no production,loaded/reverse/dual-wheel,soak or kernel-repair acceptance is implied.
