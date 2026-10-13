# ROS2 状态年龄与外部运动授权合同

状态：2026-10-10，诊断合同已实现；下述运动 v2 是下一实现阶段的约束，**尚未接入或签发**。
本轮没有增加运动消息、挑战服务、授权状态机或 `CycleInput.external` 写入。
当前 `RCI1` 的 `probe_ok` 只表示格式、序号、板端采样时效和范围校验通过，
`executed=false`、`motion_authority=false` 永远成立，不代表命令入队、RPDO发送或车轮运动。

## 1. 当前诊断的三个时间点

| 时间/字段 | 定义和使用边界 |
| --- | --- |
| `status.captured_ns` | IPC server 在处理请求/序列化回复时的板端 `CLOCK_MONOTONIC` 时间，不是CAN反馈采样时间，也不是控制快照产生时间 |
| `receipt_age_ms` | 最后一个合法、同session内递增的状态在最终消费者本机收到之后的年龄；消费者用自身monotonic clock计算 |
| `source_age_ms` | 当前为 `null`，`source_age_verified=false`；未传递原始控制/反馈采样时刻，不能从receipt或IPC回复时刻推断 |

不同主机的 `CLOCK_MONOTONIC` 不共享起点；同一主机的time namespace也须明确。
禁止 `host_now - board_captured_ns`，禁止使用墙钟、ROS `/clock` 或把回调转发时刻冒充源采样时刻。
未来如增加状态源年龄，app应复制owner实际采样时间、驱动各反馈最后更新时间/有效性，
在板端计算 `snapshot_age_ns` 和各feedback age；host只显示这个**发送端当时**的年龄。
到达后的实际源年龄还包含单向传输/排队延迟；没有经验证的延迟上界就不能声称精确总年龄。
跨ROS转发不刷新该值；若能给出有证据的传输上界，必须同时显示范围、计算点和有效标志。

当前monitor只有一份最近快照。收到合法新快照开始计龄，500ms边界（`>=500ms`）过期；
同一 `server_started_ns/session` 的sequence与captured_ns须严格递增，cycles不得倒退。
新session可重置序号，这是诊断重连，不是安全防重放凭证；跨session缓存/恶意重放没有被证明可拒绝。
单个topic只能对应一个指定bridge；未经鉴权的DDS发布者和可控制同UID进程的攻击者不在诊断信任隔离内。

| monitor state | 显示行为 |
| --- | --- |
| WAITING | 没有收到控制快照，connected=null、healthy=false |
| FRESH | 合法递增快照且receipt age<500ms；healthy仍由最新快照的healthy决定 |
| DISCONNECTED | bridge明确报告IPC断开，立即healthy=false，不等500ms |
| STALE | 500ms无合法新快照或本机时钟倒退；connected=null、healthy=false |
| INVALID | 非法/重放/倒退状态，立即healthy=false；之后合法新状态可恢复 |

`connected` 是有效诊断结论，`ipc_connected_reported` 保留bridge的最近IPC连接报告；
STALE时它可能仍为true，不能据此显示连接健康。`stale` 表示当前不能使用，
`receipt_expired` 仅表示接收年龄到期。拒绝probe的事件不刷新快照年龄，也不撤掉仍新鲜的诊断。
monitor steady timer每100ms评估并发布 `~/diagnostics`，状态改变或每秒打印一次。
500ms是逻辑阈值，实际输出在下一次timer执行时发生；Linux调度可能继续延迟，无硬实时保证。
monitor退出后其下游也必须自行检查receipt age，不能永久显示它最后发布的healthy。

## 2. 运动 v2 的身份与时序

运动必须使用明确独立版本/消息，未知版本或kind拒绝；不得扩大RCI1的kind1含义。
app拥有挑战与授权记录，ROS仅转发不可变候选；唯一control owner最终决定是否生成CommandSample。

拟定的最小字段如下，具体ROS IDL/编码在实现切片冻结；逻辑RPM为有符号整数，
按现有RuntimePolicy做右轮符号适配，不在ROS重复翻转。

| 字段 | 签发者/约束 |
| --- | --- |
| `version` | 明确运动v2；不能与诊断version1互换 |
| `app_instance_id` | app每次启动生成的新随机128-bit实例身份；不得用monotonic启动数替代全局唯一身份 |
| `producer_id` | 启动配置允许的、可核验的来源身份；DDS topic/GID或任意字符串本身不是鉴权 |
| `connection_generation` | app本实例内正uint64，实际连接/producer登记重建时递增；溢出永久抑制 |
| `authorization_generation` | app签发，精确绑定实例/来源/连接/drive generation；恢复或切源必须新代际 |
| `drive_generation` | 当前RuntimeSession的epoch/layout有效代际；必须与owner当前值完全相等 |
| `challenge_id` | app的随机128-bit单次挑战；只认app记录，绝对deadline留在板端，不接受客户端自填deadline |
| `sequence` | 每个授权代际正uint64严格递增，不回绕；拒绝后的旧序号不得重新提交 |
| `left_rpm/right_rpm` | 原始候选逻辑目标；格式/符号/范围/溢出验证后才可进入owner |
| `stop_requested` | 明确布尔；true表示零目标/撤权，永不作为非零输出捷径 |
| `sampling_evidence` | 本机原始采样时间和已证明的clock domain，或下面的远程post-challenge采样证据；缺失拒绝 |

本机路径要求同内核、同time namespace的采样时间证据，`0 <= owner_now-captured_at < external_timeout`。
跨机路径禁止让bridge收到DDS后以本机now给旧命令换时间：

1. app在板端记录挑战发出时刻t0、绑定全部身份/代际和到期时间；每个producer至多一个在途挑战。
2. 已核验的producer收到这个挑战**以后重新采样/重新计算**，返回这个挑战及完整候选；
   重贴缓存输出或缓存授权没有采样证据，不允许自动回复。
3. owner记录验证时刻t1；只有已消费之前、绑定精确匹配且 `0<=t1-t0<external_timeout` 才接受。
   因为采样在t0以后，t1-t0可作为采样年龄的保守上界；在CommandSample中用板端t0作为保守captured_at，
   不使用收到回复时刻t1。因此后续cycle仍会自然到期，不延长该命令的生命周期。
4. 挑战回应进入校验即消耗挑战/序号，拒绝不允许修补后重放。重复请求/超时令牌均拒绝。
   新挑战不能自动复用旧候选；保持命令也必须重新采样和满足新的挑战。

挑战约束往返时效，**不单独证明源采样行为或网络身份**。未知producer、无可信post-challenge
采样契约、未具备鉴权的远程通道默认禁用外部运动；需要在实现切片声明具体信任边界并验证。
当前固定UDP profile/UFW allow只限制通信范围，不提供DDS鉴权，也不授予外部运动资格。
本合同不要求生产者实时性承诺，往返超过100ms初始external_timeout时拒绝/安全停止，
不能为慢网络默默放大deadline。100ms沿用现有ArbiterConfig默认；启动注入的最终值需经测试冻结。
诊断500ms阈值不得用作运动有效期。

## 3. 授权、切源与撤权

只由control owner修改授权状态，IPC/ROS callback只能交付有界最新候选；不发送CAN、不使能、
不复位故障、不自行签发代际。复用 `ControlArbiter`、`ControlCycle` 和 `RuntimeSession`，
不在ROS增加第二个安全状态机；下面是外部适配层必须映射到现有owner的合同。

| 阶段 | 进入/退出条件 |
| --- | --- |
| REVOKED（默认） | 启动/连接/恢复都只有观察资格；输出零，没有可用运动代际 |
| NEUTRAL_QUALIFYING | 已验证身份/健康反馈/驱动代际/安全状态；相关来源提供新鲜中立样本，双轴目标零并满足现有standstill检查 |
| AUTHORIZED_ZERO | 完成连续零保持（现有默认150ms，不跳过）、非保持的显式新授权事件，owner完成CiA402零目标过渡，签发新代际；仍先输出零 |
| ACTIVE | 后续新挑战、新序号、有效原始采样、精确代际、健康反馈及安全状态全部成立才可接受非零候选 |

非中立授权请求拒绝；不能靠先请求授权再零保持来缓存授权事件。
中立资格中途过期/失联/非零/故障会清空进度；重新中立后需要新的明确授权事件。
普通中立候选使目标为零，但不会自动切源或凭空建立授权。
模式选择本身不是运动权限：需要新鲜SBUS模式输入及既有授权/切源条件；持住旧使能按钮不是新事件。

SBUS与external切换必须先撤销原来源代际、输出精确零，完成连续零保持及新的授权generation；
旧external候选在SBUS接管后永久失效，即使时间仍新鲜。接管手动SBUS同样遵循安全零过渡，
不是直接把非中立SBUS目标瞬间接到驱动。离开SBUS再回来也需要新的授权条件。
重新获准时待发队列清空，初次命令必须是新采样；不能恢复撤权前的非零目标。

以下事件在owner下一次有界处理时撤权、清空候选、作废挑战/授权并执行现有零/quick-stop/安全禁能路径：
DDS/IPC连接失效或采样/挑战超时、producer或app重启、drive epoch/布局变化、反馈/heartbeat失效、
CAN失联、SBUS failsafe/失效、急停、shutdown和控制输入不一致。
所有抑制优先于运动；shutdown后不可再重授权。重建通信只返回REVOKED。
物理急停优先且独立于ROS；CAN不可发送时不能声称已送出零，必须保留发送失败和独立保护证据。
退出先撤权，再运行现有有界停止/清理；不能把500ms显示过期当成实际停止期限。

运动结果须分别报告 `validated / owner_selected / transport_submitted / feedback_observed`，
每一步关联实例/代际/序号。仅收到IPC回复不得声明执行；发送成功也不等于机械运动/安全停车。
诊断probe始终只有 `probe_ok`，不能复用任何上述运动结果标签。

## 4. 下一实现切片的可执行验收矩阵

本轮执行的是诊断/安装回归与既有control_cycle/IPC contract；运动v2矩阵尚未实现，不标为PASS。
实现前先为实际owner/适配入口补充以下失败回归，再连接external；不写独立影子模型冒充实现验证。

| 用例 | 必须观察的结果 |
| --- | --- |
| 年龄=timeout、未来时间、时钟倒退、跨机直接monotonic值、缺sampling evidence | 拒绝，零；转发不能刷新年龄 |
| 挑战恰好到期、旧/已消费挑战、旧序号、回绕、篡改绑定身份 | 拒绝，无代际/队列刷新 |
| 新challenge附旧缓存候选、旧授权重新签名/包装 | 无可信采样证据即拒绝 |
| app/producer/IPC重启、drive generation变化、切到SBUS后返回 | 旧候选与旧代际无效；健康通信恢复仍REVOKED |
| 非中立新授权、150ms边界前授权、保持按钮、资格期间失联 | 不使能/清空资格；之后需新事件 |
| 正常双源切换、抢占、ROS publisher死亡、executor暂停/积压 | 零保持，新授权；无旧队列复活；bounded owner处理 |
| 有效新候选但反馈/heartbeat/CAN失效、急停、SIGTERM同周期出现 | 抑制优先；不发送新非零；失败回执保留 |
| RCI1 probe_ok以及monitor FRESH/healthy | 无external提交，无运动权限，无新增CAN命令 |

验收层次：纯逻辑边界 → managed vcan/PTY真实闭环 → Debug/sanitizer/交叉构建/静态 →
设备无访问目标smoke → 独立授权的零目标现场试验。任何一层都不替代后续物理停车验收。
RTK、Twist、物理尺度、运动HIL、延期CAN/单侧故障残余继续按[实施计划](ROS2_IPC_NEXT_STEPS.md)分开推进。
