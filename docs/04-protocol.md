# Socket 通信协议 v1

更新时间：2026-09-05

## 1. 帧格式

- TCP，默认端口 `45454`。
- 一帧为 `4 字节无符号大端 JSON 长度 + UTF-8 JSON`，长度不含自身。
- 单帧上限 1 MiB；长度为 0、超过上限或 JSON 非对象时返回协议错误并断开。
- 两端使用 `libs/common` 的同一缓冲解析器，支持半包、粘包和连续多帧。

请求与响应使用如下信封：

```json
{"v":1,"type":"station.list","requestId":"uuid","sessionToken":"login-result-token","timestampMs":1788288000000,"data":{}}
```

```json
{"v":1,"type":"station.list.result","requestId":"same-uuid","ok":true,"timestampMs":1788288000100,"data":{}}
```

错误统一为 `type=error`，`error` 包含稳定 `code`、中文 `message` 和 `retryable`。服务器事件使用独立 `eventId`，没有 `requestId`。

## 2. 已实现请求

无需会话：

- `ping`
- `session.loginByPhone { phone }`

用户与钱包：

- `session.logout`：使当前连接的会话令牌立即失效，连接保留以便重新登录其他账号。
- `user.getProfile`
- `user.updateProfile { nickname, avatarBase64 }`
- `wallet.recharge { amountCents }`

地图与电站：

- `location.geocode { address }`
- `station.list { latitude, longitude }`
- `station.get { stationId }`，返回电站和站内全部电桩

订单：

- `order.getActive`
- `order.listMine`
- `order.reserve { chargerId }`
- `order.startCharging { orderId }`
- `order.cancelReservation { orderId }`
- `order.stopCharging { orderId }`
- `order.settle { orderId }`
- 订单数据包含 `energyWh`、`targetEnergyWh` 和 `progressPercent`；进度由服务器统一计算。
- `order.updated`：只推送给同一用户的所有在线会话；`data.change=state` 表示业务状态变化（包括充满自动停止）并触发订单记录刷新，`meter` 表示每秒计量且只局部更新当前订单。
- `stations.changed`：预约、结算、新增电站或重启电桩后广播，客户端重新查询附近站点。
- `user.updated`：资料、充值或结算余额变化后推送给同一用户会话。
- `user.statusChanged`：管理员冻结/解冻事件；冻结时服务器同时撤销该用户在线会话。

管理 GUI 与服务器同进程，通过 queued signal 调用同一工作线程，不开放管理员网络协议。

## 3. 会话、连接与重复请求

- 登录成功返回随机 `sessionToken`；之后所有用户业务请求必须携带并匹配当前连接令牌。
- 客户端每 15 秒心跳；服务器 45 秒未收到数据主动断开。
- 客户端采用 1～10 秒有上限退避重连；重连后用原手机号重新登录并请求资料、站点、活动订单和历史订单。
- 同一连接重复发送相同 `requestId` 时，服务器返回首次响应，不重复执行充值或订单写入；单连接保留最近最多约 200 项响应。
- 结算在数据库层额外幂等：已结算订单再次提交直接返回原结果，不再次扣款。部分唯一索引和事务保证同一用户/电桩最多一笔活动订单。
- 登录失败会先清除该连接原有认证状态，避免旧会话在冻结或错误重登录后继续有效。
- 主动退出后服务器清除该连接的用户和令牌；旧令牌再请求业务会返回 `UNAUTHENTICATED`，同一 TCP 连接仍可再次登录。

## 4. 错误码

主要错误码：`PROTOCOL_INVALID_FRAME`、`PROTOCOL_UNSUPPORTED_VERSION`、`REQUEST_INVALID`、`UNAUTHENTICATED`、`USER_FROZEN`、`USER_NOT_FOUND`、`STATION_NOT_FOUND`、`CHARGER_NOT_FOUND`、`CHARGER_NOT_AVAILABLE`、`ACTIVE_ORDER_EXISTS`、`ORDER_NOT_FOUND`、`ORDER_STATE_CONFLICT`、`INSUFFICIENT_BALANCE`、`MAP_SERVICE_UNAVAILABLE`、`DATABASE_UNAVAILABLE`。

头像在客户端缩放并压缩为 JPEG 后以 Base64 传输，服务端限制提交大小并在 SQLite 中保存 Base64；该取舍适合少量课堂演示数据，不作为生产存储方案。
