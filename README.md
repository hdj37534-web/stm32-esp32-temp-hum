# STM32 + ESP32 温湿度监控系统

基于 STM32F103C8T6、DHT11、SSD1306 OLED 和 ESP32-WROOM-32 的温湿度监控课设项目。

STM32 负责温湿度采集、本地显示、阈值报警和 LED 控制；ESP32 通过 MQTT 将数据同步至 OneNET；Android APP 从 OneNET 读取数据，并可远程控制蜂鸣器和 LED。

## 功能

- 每 2 秒采集一次 DHT11 温度、湿度。
- OLED 固定区域局部刷新，避免整屏清屏带来的闪烁。
- KEY1 切换主界面、温度阈值界面、湿度阈值界面；KEY2 增加阈值，KEY3 减少阈值。
- 默认温度阈值为 35 C，湿度阈值为 80 %；温度或湿度严格高于对应阈值时，有源蜂鸣器报警。
- OneNET 上报 `Temp`、`Hum`、`Alarm`、`Led` 四个物模型属性。
- APP 可远程控制 `Alarm` 和 `Led`，控制结果由 STM32 实际状态回传云端。

## 系统架构

```text
DHT11 / Keys / OLED / Buzzer / LED
              |
         STM32F103C8T6
              | UART2, 115200 8N1
         ESP32-WROOM-32
              | WiFi + MQTT
            OneNET
              |
         Android APP
```

## 硬件连接
<img width="3072" height="4096" alt="dbe46f6f7f5d402aeaa6f08b5535fc1b" src="https://github.com/user-attachments/assets/cc332754-7b85-4ec3-84ae-1a7a775880bb" />
<img width="3072" height="4096" alt="eeee3d85f38d3a154e943d59cb073a4b" src="https://github.com/user-attachments/assets/9b7aad21-ef37-4d79-92ae-51f9c0bf9914" />

| 模块 | STM32 引脚 | 说明 |
| --- | --- | --- |
| DHT11 DATA | PB12 | 单总线温湿度数据 |
| OLED SCL | PB8 | SSD1306 软件 I2C |
| OLED SDA | PB9 | SSD1306 软件 I2C |
| KEY1 | PA5 | 界面切换 |
| KEY2 | PA6 | 阈值加 1 |
| KEY3 | PA7 | 阈值减 1 |
| 有源蜂鸣器 | PB13 | 报警输出 |
| LED | PA4 | 本地/远程控制 |
| STM32 USART2 TX | PA2 | 接 ESP32 GPIO16 (RX2) |
| STM32 USART2 RX | PA3 | 接 ESP32 GPIO17 (TX2) |

STM32 与 ESP32 必须共地。ESP32 的 GPIO 为 3.3 V 电平，STM32F103 使用 3.3 V 供电时可直接连接。

## 工程结构

```text
hardware/                    STM32 应用逻辑、DHT11 与 OLED 驱动
BSP/                         STM32 按键、定时器、串口、蜂鸣器、LED 驱动
communication/               STM32 端 UART2 帧通信模块
Main/                        STM32 程序入口与中断模板
Project/RVMDK（uv5）/         Keil5 工程
ESP32/                       ESP32 PlatformIO (Arduino) 工程
APP源码/温湿度/               uni-app 源码
apk/                         已打包的 Android 安装包
Output/                      STM32 编译输出
```

## 烧录与运行

### 1. STM32

1. 使用 Keil5 打开 `Project/RVMDK（uv5）/BH-F103.uvprojx`。
2. 编译并下载，或烧录 `Output/流水灯.hex`。
3. 上电后 OLED 显示温湿度主界面。首次采样后会将实际状态发送给 ESP32。

### 2. ESP32

1. 使用 VS Code + PlatformIO 打开 `ESP` 目录。
2. 在 `ESP32/src/main.cpp` 顶部填写本地 WiFi 名称和密码，并确认 OneNET 产品、设备及 MQTT Token 配置。
3. 选择 `esp32dev` 环境，编译、烧录 ESP32。
4. 打开串口监视器，波特率为 `115200`。出现 `Connected, subscribe sent` 后，ESP32 即可收发 OneNET 数据。

### 3. Android APP

在 Android 设备上安装 [__UNI__98A711F__20260816005224.apk](apk/__UNI__98A711F__20260816005224.apk)。APP 通过 OneNET 查询温湿度和设备状态，并通过属性设置接口控制 LED、蜂鸣器。

## OneNET MQTT

当前使用 OneNET 新版物模型 MQTT 接入。ESP32 使用 `mqtts.heclouds.com:1883`，未启用 TLS。

| 用途 | Topic |
| --- | --- |
| 属性上报 | `$sys/{productId}/{deviceName}/thing/property/post` |
| 上报回复 | `$sys/{productId}/{deviceName}/thing/property/post/reply` |
| 属性下发 | `$sys/{productId}/{deviceName}/thing/property/set` |
| 下发确认 | `$sys/{productId}/{deviceName}/thing/property/set_reply` |

物模型属性区分大小写：

| 属性 | 类型 | 说明 |
| --- | --- | --- |
| `Temp` | Number | 温度整数，单位 C |
| `Hum` | Number | 湿度整数，单位 % |
| `Alarm` | Boolean | 蜂鸣器报警状态 |
| `Led` | Boolean | LED 状态 |

上报报文示例：

```json
{
  "id": "123",
  "params": {
    "Temp": { "value": 31 },
    "Hum": { "value": 76 },
    "Alarm": { "value": false },
    "Led": { "value": false }
  }
}
```

APP 下发报文示例：

```json
{
  "id": "18",
  "version": "1.0",
  "params": {
    "Led": true
  }
}
```

ESP32 同时兼容上述直接布尔值形式和 `{ "value": true }` 嵌套形式；收到下发后会回复：

```json
{ "id": "18", "code": 0, "msg": "success" }
```

## STM32 与 ESP32 串口协议

通信参数为 UART2、`115200 8N1`。帧格式：

```text
AA 55 Type Length Payload Checksum
```

校验和为 `Type ^ Length ^ Payload` 的逐字节异或结果。

| 方向 | Type | Payload |
| --- | --- | --- |
| STM32 -> ESP32 | `0x01` | `Temp`, `Hum`, `flags` |
| ESP32 -> STM32 | `0x81` | `flags` |

`flags` 位定义：bit0 为 `Alarm`，bit1 为 `Led`。

## 联调检查

1. STM32 OLED 是否能每 2 秒更新温湿度，且不整屏闪烁。
2. 调低本地阈值，确认蜂鸣器在温度或湿度高于阈值时响起。
3. ESP32 串口是否出现 UART 状态帧、MQTT 连接与属性上报日志。
4. OneNET 控制台是否显示设备在线，以及 `Temp`、`Hum`、`Alarm`、`Led` 的最新值。
5. 在 APP 分别控制 LED 和 Alarm，确认硬件动作且 APP 不出现“设备响应超时”。

## 注意事项

- WiFi 密码、OneNET MQTT Token 和 APP 授权信息属于敏感配置，不应提交到公开仓库。
- APP 返回 HTTP 200 但提示 `10411`，通常表示 OneNET 未收到设备对 `property/set` 的 MQTT 确认报文；检查 ESP32 是否订阅了 `property/set` 并向 `property/set_reply` 回复相同的 `id`。
- 修改 ESP32 的 WiFi 或 OneNET 配置后，需要重新编译、烧录 ESP32。
