<template>
	<view class="page">
		<view class="banner">
			<view class="banner-copy">
				<text class="banner-title">环境监测中</text>
				<text class="banner-text">实时接收单片机上传数据</text>
			</view>
			<view class="banner-illustration">
				<view class="sun"></view>
				<view class="hill hill-left"></view>
				<view class="hill hill-right"></view>
				<view class="water-line line-one"></view>
				<view class="water-line line-two"></view>
			</view>
		</view>

		<view class="section">
			<view class="data-grid">
				<view class="data-card">
					<image class="function-icon icon-temperature" src="../../Temp.jpg" mode="aspectFit"></image>
					<text class="card-label">环境温度</text>
					<view class="value-row">
						<text class="sensor-value">{{ displayTemperature }}</text>
						<text class="sensor-unit">°C</text>
					</view>
				</view>

				<view class="data-card">
					<image class="function-icon icon-humidity" src="../../Hum.png" mode="aspectFit"></image>
					<text class="card-label">环境湿度</text>
					<view class="value-row">
						<text class="sensor-value">{{ displayHumidity }}</text>
						<text class="sensor-unit">%</text>
					</view>
				</view>
			</view>
		</view>

		<view class="section control-section">
			<view class="data-grid">
				<view class="control-card" hover-class="control-hover" @tap="toggleDevice('alarm')">
					<image class="function-icon icon-alarm" src="../../Alarm.png" mode="aspectFit"></image>
					<text class="card-label">报警器</text>
					<view class="switch" :class="{ active: alarmOn }">
						<view class="switch-thumb"></view>
					</view>
				</view>

				<view class="control-card" hover-class="control-hover" @tap="toggleDevice('led')">
					<image class="function-icon icon-led" src="../../LED.png" mode="aspectFit"></image>
					<text class="card-label">LED灯</text>
					<view class="switch" :class="{ active: ledOn }">
						<view class="switch-thumb"></view>
					</view>
				</view>
			</view>
		</view>
	</view>
</template>

<script>
	const ONENET_CONFIG = {
		baseUrl: 'https://iot-api.heclouds.com',
		productId: 'KaA9E3nyKZ',
		deviceName: 'test1',
		authorization: 'version=2018-10-31&res=products%2FKaA9E3nyKZ%2Fdevices%2Ftest1&et=1818174766&method=md5&sign=5upB1tl2yxepvDHqwraUBA%3D%3D',
		pollInterval: 3000
	}

	const PROPERTY_KEYS = {
		temperature: 'Temp',
		humidity: 'Hum',
		alarm: 'Alarm',
		led: 'Led'
	}

	const CONTROL_OPTIONS = {
		alarm: {
			propertyKey: PROPERTY_KEYS.alarm,
			stateKey: 'alarmOn'
		},
		led: {
			propertyKey: PROPERTY_KEYS.led,
			stateKey: 'ledOn'
		}
	}

	export default {
		data() {
			return {
				temperature: 26.4,
				humidity: 58.2,
				ledOn: false,
				alarmOn: false,
				pollTimer: null,
				mockTimer: null,
				// 控制指令回显前，避免旧轮询结果覆盖乐观更新的界面状态。
				controlPending: {
					led: false,
					alarm: false
				},
				controlLocked: {
					led: false,
					alarm: false
				},
				controlTarget: {
					led: null,
					alarm: null
				},
			}
		},
		computed: {
			apiReady() {
				return Boolean(ONENET_CONFIG.authorization)
			},
			displayTemperature() {
				return Number(this.temperature).toFixed(1)
			},
			displayHumidity() {
				return Number(this.humidity).toFixed(1)
			}
		},
		onLoad() {
			this.startDataRefresh()
		},
		onUnload() {
			this.stopDataRefresh()
		},
		methods: {
			startDataRefresh() {
				this.stopDataRefresh()

				if (!this.apiReady) {
					this.startMockRefresh()
					return
				}

				this.refreshDeviceData()
				this.pollTimer = setInterval(() => {
					this.refreshDeviceData()
				}, ONENET_CONFIG.pollInterval)
			},
			stopDataRefresh() {
				if (this.pollTimer) {
					clearInterval(this.pollTimer)
					this.pollTimer = null
				}

				this.stopMockRefresh()
			},
			refreshDeviceData() {
				if (!this.apiReady) {
					uni.showToast({
						title: '请先配置OneNET',
						icon: 'none'
					})
					return
				}

				uni.request({
					url: `${ONENET_CONFIG.baseUrl}/thingmodel/query-device-property`,
					method: 'GET',
					header: this.getOneNetHeaders(),
					data: {
						product_id: ONENET_CONFIG.productId,
						device_name: ONENET_CONFIG.deviceName
					},
					success: (response) => {
						const body = this.getResponseBody(response)

						if (this.isSuccessResponse(body, response.statusCode)) {
							this.applyOneNetProperties(body.data || body)
						}
					}
				})
			},
			toggleDevice(type) {
				const control = CONTROL_OPTIONS[type]

				if (!control || this.controlLocked[type]) {
					return
				}

				const nextState = !this[control.stateKey]
				this.controlLocked[type] = true
				this[control.stateKey] = nextState
				this.sendControlCommand(type, control, nextState)
			},
			sendControlCommand(type, control, enabled) {
				if (!this.apiReady) {
					this.controlLocked[type] = false
					uni.showToast({
						title: enabled ? '已开启' : '已关闭',
						icon: 'none'
					})
					return
				}

				this.controlPending[type] = true
				this.controlTarget[type] = enabled

				uni.request({
					url: `${ONENET_CONFIG.baseUrl}/thingmodel/set-device-property`,
					method: 'POST',
					header: this.getOneNetHeaders(),
					data: {
						product_id: ONENET_CONFIG.productId,
						device_name: ONENET_CONFIG.deviceName,
						params: {
							[control.propertyKey]: enabled
						}
					},
					success: (response) => {
						const body = this.getResponseBody(response)

						if (!this.isSuccessResponse(body, response.statusCode)) {
							this.handleControlFailure(type, control.stateKey, enabled, response.statusCode, body)
							return
						}

					// 请求已完成，可立即再次点击；待确认状态继续过滤旧轮询结果。
						this.controlLocked[type] = false
						setTimeout(() => {
							this.refreshDeviceData()
						}, 800)
					},
					fail: (error) => {
						this.handleControlRequestError(type, control.stateKey, enabled, error)
					}
				})
			},
			resetControlState(type) {
				this.controlLocked[type] = false
				this.controlPending[type] = false
				this.controlTarget[type] = null
			},
			handleControlFailure(type, stateKey, enabled, statusCode, body) {
				this[stateKey] = !enabled
				this.resetControlState(type)

				const errorCode = body.errno !== undefined ? body.errno : (body.code !== undefined ? body.code : statusCode)
				const errorMessage = body.msg || body.message || '未知错误'
				console.error('OneNET property set failed:', statusCode, body)
				uni.showToast({
					title: `失败 ${errorCode}: ${errorMessage}`,
					icon: 'none'
				})
			},
			handleControlRequestError(type, stateKey, enabled, error) {
				this[stateKey] = !enabled
				this.resetControlState(type)
				console.error('OneNET property set request failed:', error)
				uni.showToast({
					title: `请求失败: ${error.errMsg || '网络异常'}`,
					icon: 'none'
				})
			},
			getResponseBody(response) {
				return response.data || {}
			},
			getOneNetHeaders() {
				return {
					Authorization: ONENET_CONFIG.authorization,
					'Content-Type': 'application/json'
				}
			},
			isSuccessResponse(body, statusCode) {
				const code = body.errno !== undefined ? body.errno : body.code
				return statusCode >= 200 && statusCode < 300 && (code === undefined || Number(code) === 0)
			},
			applyOneNetProperties(data) {
				const properties = this.normalizeProperties(data)

				this.assignNumberProperty(properties, PROPERTY_KEYS.temperature, 'temperature')
				this.assignNumberProperty(properties, PROPERTY_KEYS.humidity, 'humidity')
				this.assignBooleanProperty(properties, PROPERTY_KEYS.led, 'ledOn', 'led')
				this.assignBooleanProperty(properties, PROPERTY_KEYS.alarm, 'alarmOn', 'alarm')
			},
			normalizeProperties(data) {
				if (Array.isArray(data)) {
					return data.reduce((result, item) => {
						const key = item.identifier || item.key || item.name || item.id
						const value = item.value !== undefined ? item.value : item.current_value

						if (key) {
							result[key] = this.unwrapPropertyValue(value)
						}

						return result
					}, {})
				}

				if (data && Array.isArray(data.list)) {
					return this.normalizeProperties(data.list)
				}

				if (data && Array.isArray(data.properties)) {
					return this.normalizeProperties(data.properties)
				}

				const source = data && data.params ? data.params : data

				if (!source || typeof source !== 'object') {
					return {}
				}

				return Object.keys(source).reduce((result, key) => {
					result[key] = this.unwrapPropertyValue(source[key])
					return result
				}, {})
			},
			unwrapPropertyValue(value) {
				if (value && typeof value === 'object' && value.value !== undefined) {
					return value.value
				}

				return value
			},
			assignNumberProperty(properties, propertyKey, stateKey) {
				if (properties[propertyKey] === undefined) {
					return
				}

				const nextValue = Number(properties[propertyKey])
				if (!Number.isNaN(nextValue)) {
					this[stateKey] = nextValue
				}
			},
			assignBooleanProperty(properties, propertyKey, stateKey, controlKey) {
				if (properties[propertyKey] === undefined) {
					return
				}

				const nextValue = properties[propertyKey] === true || properties[propertyKey] === 'true'

				if (this.controlPending[controlKey]) {
					if (nextValue !== this.controlTarget[controlKey]) {
						return
					}

					this.controlPending[controlKey] = false
					this.controlTarget[controlKey] = null
				}

				this[stateKey] = nextValue
			},
			startMockRefresh() {
				this.stopMockRefresh()
				this.mockTimer = setInterval(() => {
					const temperatureOffset = (Math.random() - 0.5) * 0.4
					const humidityOffset = (Math.random() - 0.5) * 0.8

					this.temperature = this.limitValue(this.temperature + temperatureOffset, 18, 36)
					this.humidity = this.limitValue(this.humidity + humidityOffset, 35, 85)
				}, 2000)
			},
			stopMockRefresh() {
				if (this.mockTimer) {
					clearInterval(this.mockTimer)
					this.mockTimer = null
				}
			},
			limitValue(value, min, max) {
				return Math.min(max, Math.max(min, value))
			},
		}
	}
</script>

<style>
	page {
		background: #f7f7f5;
	}

	.page {
		min-height: 100vh;
		box-sizing: border-box;
		padding: 48rpx 32rpx 56rpx;
		color: #202124;
		background: #f7f7f5;
	}

	.banner {
		display: flex;
		align-items: center;
		justify-content: space-between;
		box-sizing: border-box;
		min-height: 172rpx;
		padding: 34rpx 38rpx;
		margin-bottom: 46rpx;
		border-radius: 26rpx;
		background: #ffdc70;
		box-shadow: 0 16rpx 28rpx rgba(217, 170, 45, 0.16);
	}

	.banner-copy {
		display: flex;
		flex-direction: column;
	}

	.banner-title {
		margin-bottom: 18rpx;
		font-size: 36rpx;
		font-weight: 800;
		color: #202124;
	}

	.banner-text {
		font-size: 26rpx;
		color: #5f5132;
	}

	.banner-illustration {
		position: relative;
		width: 136rpx;
		height: 106rpx;
	}

	.sun {
		position: absolute;
		top: 12rpx;
		right: 16rpx;
		width: 22rpx;
		height: 22rpx;
		border-radius: 50%;
		background: #ff9e53;
	}

	.hill {
		position: absolute;
		bottom: 26rpx;
		width: 0;
		height: 0;
		border-left: 38rpx solid transparent;
		border-right: 38rpx solid transparent;
		border-bottom: 70rpx solid #ffffff;
	}

	.hill-left {
		left: 12rpx;
		border-bottom-color: #36b37e;
	}

	.hill-right {
		right: 8rpx;
		border-bottom-color: #f4f7fb;
	}

	.water-line {
		position: absolute;
		left: 6rpx;
		right: 6rpx;
		height: 5rpx;
		border-radius: 6rpx;
		background: #48a6c8;
	}

	.line-one {
		bottom: 14rpx;
	}

	.line-two {
		bottom: 2rpx;
	}

	.section {
		margin-bottom: 42rpx;
	}

	.data-grid {
		display: flex;
		justify-content: space-between;
	}

	.data-card,
	.control-card {
		display: flex;
		flex-direction: column;
		align-items: center;
		justify-content: center;
		box-sizing: border-box;
		width: calc((100% - 34rpx) / 2);
		min-height: 262rpx;
		padding: 26rpx 16rpx;
		border-radius: 8rpx;
		background: #ffffff;
		box-shadow: 0 14rpx 30rpx rgba(35, 35, 35, 0.08);
	}

	.control-card {
		min-height: 246rpx;
		transition: transform 0.16s ease, box-shadow 0.16s ease;
	}

	.control-hover {
		transform: scale(0.98);
		box-shadow: 0 8rpx 20rpx rgba(35, 35, 35, 0.06);
	}

	.function-icon {
		width: 132rpx;
		height: 132rpx;
		margin-bottom: 14rpx;
	}

	.icon-humidity,
	.icon-alarm {
		width: 150rpx;
		height: 150rpx;
		margin-top: -9rpx;
		margin-bottom: 5rpx;
	}

	.card-label {
		margin-bottom: 10rpx;
		font-size: 27rpx;
		color: #8c8c8c;
	}

	.value-row {
		display: flex;
		align-items: flex-end;
		justify-content: center;
		min-height: 54rpx;
	}

	.sensor-value {
		font-size: 42rpx;
		font-weight: 800;
		line-height: 1;
		color: #6d6d77;
	}

	.sensor-unit {
		margin-left: 6rpx;
		font-size: 24rpx;
		font-weight: 700;
		line-height: 1.15;
		color: #a0a0a8;
	}

	.switch {
		position: relative;
		width: 74rpx;
		height: 42rpx;
		margin-top: 6rpx;
		border-radius: 24rpx;
		background: #e7e7e7;
		transition: background 0.18s ease;
	}

	.switch.active {
		background: #5bd398;
	}

	.switch-thumb {
		position: absolute;
		left: 4rpx;
		top: 4rpx;
		width: 34rpx;
		height: 34rpx;
		border-radius: 50%;
		background: #ffffff;
		box-shadow: 0 4rpx 10rpx rgba(0, 0, 0, 0.16);
		transition: transform 0.18s ease;
	}

	.switch.active .switch-thumb {
		transform: translateX(32rpx);
	}

	@media screen and (max-width: 360px) {
		.page {
			padding-left: 22rpx;
			padding-right: 22rpx;
		}

		.data-card,
		.control-card {
			width: calc((100% - 24rpx) / 2);
		}

		.banner {
			padding-left: 28rpx;
			padding-right: 24rpx;
		}
	}
</style>
