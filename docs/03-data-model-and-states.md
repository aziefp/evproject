# 数据模型、业务约束与状态机

更新时间：2026-09-05

## 1. 计费规则

本阶段采用可解释的模拟公式：

```text
energy_kwh = power_kw × simulated_duration_seconds / 3600
amount_cents = round(energy_kwh × price_cents_per_kwh)
```

- 订单开始时保存站点单价快照，之后修改站点价格不得改变历史订单。
- 数据库存金额整数分，显示时转换为元，避免浮点累计误差。
- 数据库存电量整数 Wh，显示时转换为 kWh。
- 默认演示倍率为现实时间的 60 倍，即现实 1 秒代表模拟 1 分钟；倍率可配置。
- 每笔演示订单的满电目标统一为 20,000 Wh；进度为 `min(energy_wh / 20000, 100%)`，达到 100% 时服务器将电量精确封顶并自动进入待结算。
- 余额不足时服务器自动停止计量并进入待结算，最终扣款不得令余额为负。

## 2. 核心表

服务器启动时以 `CREATE TABLE/INDEX IF NOT EXISTS` 建立 schema，字段基线如下。

### users

`id, phone UNIQUE, nickname, avatar_base64, balance_cents CHECK >= 0, status, created_at, updated_at`

状态：`active | frozen`。

### admins

`id, username UNIQUE, password_salt, password_hash, status, created_at, last_login_at`

### stations

`id, name, address, latitude, longitude, price_cents_per_kwh, enabled, created_at, updated_at`

经纬度必须在合法范围；站点在线率由所属桩状态动态计算，不重复存储。

### chargers

`id, code UNIQUE, station_id FK, type, power_watts, status, total_sessions, total_duration_seconds, created_at, updated_at`

类型：`fast | slow`。状态见下文。

### orders

`id, order_no UNIQUE, user_id FK, station_id FK, charger_id FK, status, reserved_at, started_at, ended_at, settled_at, power_watts_snapshot, price_cents_snapshot, energy_wh, amount_cents, version, created_at, updated_at`

`version` 用于防止过期更新；状态和时间字段必须相互匹配。

### wallet_transactions

`id, user_id FK, order_id FK NULL, type, amount_cents, balance_after_cents, created_at, note`

类型：`recharge | charge_payment | admin_adjustment`。余额变化和流水必须在同一事务完成。

## 3. 强约束

- 同一用户最多一个活动订单：`reserved | charging | pending_settlement`。
- 同一电桩最多一个活动订单。
- 只有 `active` 用户可以新建预约或充值；冻结前已有订单仍允许本人或管理员结算，以免长期占桩。
- 只有 `idle` 电桩可以预约。
- 结算必须幂等；对已结算订单再次请求返回原结果，不再次扣款。
- 删除基础数据不是第一阶段功能；站点和电桩使用 `enabled/status` 软停用。

SQLite 使用部分唯一索引表达活动订单约束，并开启：

```sql
PRAGMA foreign_keys = ON;
PRAGMA journal_mode = WAL;
PRAGMA busy_timeout = 5000;
```

## 4. 订单状态机

```text
reserved ──start──► charging ──stop/balance limit──► pending_settlement
    │                    │                                      │
    └──cancel────────► cancelled                              └──pay/admin settle──► settled
```

- `reserved`：预约已占桩，尚未开始计量。
- `charging`：周期更新电量和预估金额。
- `pending_settlement`：计量停止，桩仍不可被其他用户使用，等待扣款。
- `settled`：费用已扣，桩恢复闲置。
- `cancelled`：未开始即取消或超时，不扣费，桩恢复闲置。

第一阶段不实现退款和已结算订单撤销。

## 5. 电桩状态机

```text
idle ──reserve──► reserved ──start──► charging ──settle──► idle
  │                    │                │
  └────fault──────────►fault◄───────────┘
                         │
                    remote restart
                         ▼
                     restarting ──success──► idle
```

演示中的故障桩不能预约。管理端可对故障桩执行 `fault → restarting → idle` 的模拟远程重启；充电中的故障注入不属于第一阶段基础实现。

## 6. 演示数据规模

以说明书截图为量级，初始化数据保持小而完整：

- 6 个深圳模拟电站。
- 32 个电桩，覆盖快充/慢充、闲置/充电/故障。
- 8 个模拟用户，包括正常、余额不足、待结算和冻结场景。
- 最近 30 日约 60 笔已结算订单，用于 7/30 日曲线和营收指标。
- 1 笔充电中订单、1 笔待结算订单和 3 个故障桩，用于演示状态处理。

模拟手机号只使用说明书风格的固定测试号码，并在登录页标明其场景，不引用真实个人数据。
