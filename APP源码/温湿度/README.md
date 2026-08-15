# 温湿度采集系统 APP

基于 uni-app 的移动端温湿度监测与设备控制应用。APP 通过 OneNET 开放 API 获取设备属性，并向设备下发 LED、报警器控制指令。

## 功能

- 定时读取并展示温度 `Temp`、湿度 `Hum`。
- 远程控制 LED 属性 `Led` 与报警器属性 `Alarm`。
- 控制时先更新开关界面；请求失败会自动回滚。
- 控制请求成功后立即允许再次操作，同时忽略指令回显前的旧轮询数据，避免开关状态跳变。
- 未配置 OneNET 鉴权信息时，使用本地模拟温湿度数据方便页面预览。

## 运行

1. 使用 HBuilderX 导入本目录为 uni-app 项目。
2. 在 `pages/index/index.vue` 的 `ONENET_CONFIG` 中确认产品 ID、设备名称和 `authorization` 鉴权 Token。
3. 选择“运行到 Android App 基座”调试，或发行原生 App。

本项目以 App 端为主要运行目标；页面采用 uni-app Vue 语法，可继续适配 H5 和小程序。H5 调试需要在 OneNET 控制台或代理层处理跨域限制。

## OneNET 对接

| 用途 | 请求方式 | 地址 |
| --- | --- | --- |
| 查询设备属性 | `GET` | `/thingmodel/query-device-property` |
| 设置设备属性 | `POST` | `/thingmodel/set-device-property` |

请求基础地址为 `https://iot-api.heclouds.com`，请求头使用：

```http
Authorization: <OneNET Token>
Content-Type: application/json
```

属性标识符与设备物模型必须一致且区分大小写：

| 功能 | 属性 | 类型 |
| --- | --- | --- |
| 温度 | `Temp` | 数值 |
| 湿度 | `Hum` | 数值 |
| 报警器 | `Alarm` | 布尔值 |
| LED | `Led` | 布尔值 |

控制请求的 `params` 使用直接布尔值，不包裹 `value` 字段：

```json
{
  "product_id": "<产品 ID>",
  "device_name": "<设备名称>",
  "params": {
    "Led": true
  }
}
```

设备应在完成控制后重新上报实际属性状态。更完整的设备 MQTT Topic、上报格式和串口转发说明见 [OneNET_APP_对接说明.md](OneNET_APP_对接说明.md)。

## 项目结构

```text
温湿度/
├── pages/index/index.vue       # 页面、轮询、OneNET 请求与控制状态同步
├── pages.json                  # 页面路由及导航栏配置
├── manifest.json               # App 打包配置
├── Temp.jpg                    # 温度图标
├── Hum.png                     # 湿度图标
├── Alarm.png                   # 报警器图标
└── LED.png                     # LED 图标
```

## 安全提示

`authorization` 是设备访问凭据。实际发布或提交代码前，应将其迁移到安全的服务端签发流程或本地私有配置中，避免将有效 Token 提交到公开仓库。
