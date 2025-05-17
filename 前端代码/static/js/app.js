// 全局状态
let deviceOnlineStatus = false;
let mqttConnectedStatus = false;
let currentControlMode = 'manual';
let currentThresholds = {
    "water": {"min": 30, "max": 70, "unit": "%"},
    "temp": {"min": 20, "max": 30, "unit": "C"},
    "humi": {"min": 40, "max": 70, "unit": "%"},
    "light": {"min": 500, "max": 2000, "unit": "lux"}
};

// 传感器图表
let sensorCharts = {
    water: null,
    temp: null,
    humi: null,
    light: null
};

// 图表颜色配置
const chartColors = {
    water: {
        borderColor: 'rgba(54, 162, 235, 1)',
        backgroundColor: 'rgba(54, 162, 235, 0.2)'
    },
    temp: {
        borderColor: 'rgba(255, 99, 132, 1)',
        backgroundColor: 'rgba(255, 99, 132, 0.2)'
    },
    humi: {
        borderColor: 'rgba(75, 192, 192, 1)',
        backgroundColor: 'rgba(75, 192, 192, 0.2)'
    },
    light: {
        borderColor: 'rgba(255, 159, 64, 1)',
        backgroundColor: 'rgba(255, 159, 64, 0.2)'
    }
};

// 传感器单位
const sensorUnits = {
    water: '%',
    temp: '°C',
    humi: '%',
    light: 'lux'
};

// DOM元素
const connectionStatus = document.getElementById('connection-status');
const sensorElements = {
    water: document.getElementById('water-value'),
    temp: document.getElementById('temp-value'),
    humi: document.getElementById('humi-value'),
    light: document.getElementById('light-value')
};

const deviceSwitches = {
    fan: document.getElementById('fan-switch'),
    exh: document.getElementById('exh-switch'),
    pum: document.getElementById('pum-switch')
};

const cameraImage = document.getElementById('camera-image');
const noImageMessage = document.getElementById('no-image-message');
const imageInfo = document.getElementById('image-info');

// 控制模式元素
const controlModeRadios = document.querySelectorAll('input[name="controlMode"]');
const manualControlPanel = document.getElementById('manual-control-panel');
const autoControlPanel = document.getElementById('auto-control-panel');
const thresholdInputs = document.querySelectorAll('.threshold-input');
const saveThresholdsBtn = document.getElementById('save-thresholds');
const presetButtons = document.querySelectorAll('.preset-mode-btn');
const currentPresetName = document.getElementById('current-preset-name');

// 全局变量记录当前预设模式
let currentPreset = 'normal';
let presetModes = {};

// 初始化
document.addEventListener('DOMContentLoaded', () => {
    // 注册Chart.js插件
    if (Chart.annotation) {
        Chart.register(Chart.annotation);
    }
    
    // 初始化控制开关事件监听
    Object.entries(deviceSwitches).forEach(([device, switchElem]) => {
        if (switchElem) {
            switchElem.addEventListener('change', () => {
                const isOn = switchElem.checked;
                sendControlCommand(device, isOn ? 'on' : 'off');
                switchElem.nextElementSibling.textContent = isOn ? '开启' : '关闭';
            });
        }
    });

    // 初始化控制模式切换
    controlModeRadios.forEach(radio => {
        radio.addEventListener('change', function() {
            if (this.checked) {
                switchControlMode(this.value);
            }
        });
    });

    // 保存阈值按钮事件
    if (saveThresholdsBtn) {
        saveThresholdsBtn.addEventListener('click', saveThresholds);
    }

    // 预设模式按钮事件
    presetButtons.forEach(btn => {
        btn.addEventListener('click', function() {
            const preset = this.dataset.preset;
            applyPresetMode(preset);
        });
    });
    
    // 初始化阈值输入状态（默认禁用，除非已经是自定义模式）
    toggleThresholdInputs(currentPreset === 'custom');
    
    // 添加刷新图表按钮事件
    const refreshChartBtn = document.getElementById('refresh-chart');
    if (refreshChartBtn) {
        refreshChartBtn.addEventListener('click', () => {
            // 显示刷新动画
            refreshChartBtn.classList.add('rotating');
            
            // 触发完整图表更新
            fetchHistory(true).finally(() => {
                // 更新完成后移除动画
                setTimeout(() => {
                    refreshChartBtn.classList.remove('rotating');
                }, 500);
            });
        });
    }

    // 初始化图表
    initCharts();
    
    // 开始数据轮询
    fetchDataPeriodically();
    fetchImagePeriodically();
    fetchHistoryPeriodically();
});

/**
 * 切换控制模式
 */
function switchControlMode(mode) {
    if (mode === currentControlMode) return;
    
    if (!mqttConnectedStatus) {
        showAlert('MQTT服务器未连接，无法切换控制模式');
        resetControlModeRadio(currentControlMode);
        return;
    }

    if (!deviceOnlineStatus) {
        showAlert('设备离线，无法切换控制模式');
        resetControlModeRadio(currentControlMode);
        return;
    }

    // 更新UI
    if (mode === 'manual') {
        manualControlPanel.style.display = 'flex';
        autoControlPanel.style.display = 'none';
    } else {
        manualControlPanel.style.display = 'none';
        autoControlPanel.style.display = 'flex';
    }

    // 发送控制模式到服务器
    setControlMode(mode);
}

/**
 * 重置控制模式单选按钮
 */
function resetControlModeRadio(mode) {
    controlModeRadios.forEach(radio => {
        radio.checked = (radio.value === mode);
    });
}

/**
 * 设置控制模式
 */
async function setControlMode(mode) {
    try {
        const response = await fetch('/api/mode', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            },
            body: JSON.stringify({ mode })
        });

        const result = await response.json();
        
        if (result.success) {
            currentControlMode = mode;
            console.log(`控制模式已切换到: ${mode}`);
        } else {
            showAlert(`切换控制模式失败: ${result.message || '未知错误'}`);
            resetControlModeRadio(currentControlMode);
        }
    } catch (error) {
        console.error('设置控制模式失败:', error);
        showAlert('设置控制模式失败，请重试');
        resetControlModeRadio(currentControlMode);
    }
}

/**
 * 保存阈值设置
 */
async function saveThresholds() {
    if (!mqttConnectedStatus) {
        showAlert('MQTT服务器未连接，无法保存阈值设置');
        return;
    }
    
    if (currentPreset !== 'custom') {
        showAlert('只有在自定义模式下才能修改阈值设置');
        return;
    }

    const thresholdData = {};
    let hasChanges = false;

    // 收集阈值输入
    thresholdInputs.forEach(input => {
        const sensor = input.dataset.sensor;
        const type = input.dataset.type;
        const value = parseFloat(input.value);

        // 创建传感器对象（如果不存在）
        if (!thresholdData[sensor]) {
            thresholdData[sensor] = {};
        }

        // 只更新发生变化的值
        if (currentThresholds[sensor][type] !== value) {
            thresholdData[sensor][type] = value;
            hasChanges = true;
        }
    });

    if (!hasChanges) {
        showAlert('没有阈值设置变化');
        return;
    }

    try {
        const response = await fetch('/api/thresholds', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            },
            body: JSON.stringify(thresholdData)
        });

        const result = await response.json();
        
        if (result.success) {
            showAlert('阈值设置已保存');
            
            // 更新当前阈值
            Object.entries(thresholdData).forEach(([sensor, values]) => {
                Object.entries(values).forEach(([type, value]) => {
                    currentThresholds[sensor][type] = value;
                });
            });
        } else {
            showAlert(`保存阈值设置失败: ${result.message || '未知错误'}`);
        }
    } catch (error) {
        console.error('保存阈值设置失败:', error);
        showAlert('保存阈值设置失败，请重试');
    }
}

/**
 * 更新阈值输入框
 */
function updateThresholdInputs(thresholds) {
    Object.entries(thresholds).forEach(([sensor, values]) => {
        const minInput = document.getElementById(`${sensor}-min`);
        const maxInput = document.getElementById(`${sensor}-max`);
        
        if (minInput) minInput.value = values.min;
        if (maxInput) maxInput.value = values.max;
        
        // 更新图表上的阈值线
        if (sensorCharts[sensor]) {
            updateThresholdLines(sensor, sensorCharts[sensor]);
        }
    });
    
    // 更新当前阈值
    currentThresholds = thresholds;
}

/**
 * 定期获取数据
 */
function fetchDataPeriodically() {
    fetchData();
    setInterval(fetchData, 2000); // 保持每2秒更新一次数据
}

/**
 * 定期获取图像
 */
function fetchImagePeriodically() {
    fetchImage();
    setInterval(fetchImage, 5000); // 每5秒更新一次图像
}

/**
 * 定期获取历史数据
 */
function fetchHistoryPeriodically() {
    // 初始加载完整历史
    fetchHistory();
    
    // 不需要额外的定时器，fetchData中已经会触发fetchHistory
    // 但为了系统稳定性，仍然保留一个低频率的完整更新
    setInterval(() => {
        fetchHistory(true); // 参数true表示这是一个周期性的完整更新
    }, 30000); // 每30秒做一次完整更新，作为备份机制
}

/**
 * 更新预设按钮UI状态
 */
function updatePresetButtonsUI(preset) {
    presetButtons.forEach(btn => {
        if (btn.dataset.preset === preset) {
            btn.classList.add('active');
        } else {
            btn.classList.remove('active');
        }
    });
    
    if (currentPresetName && preset in presetModes) {
        currentPresetName.textContent = presetModes[preset];
    }
    
    // 根据预设模式启用或禁用阈值输入
    toggleThresholdInputs(preset === 'custom');
}

/**
 * 启用或禁用阈值输入
 */
function toggleThresholdInputs(enable) {
    // 启用或禁用所有阈值输入框
    thresholdInputs.forEach(input => {
        input.disabled = !enable;
        
        // 为父元素添加或移除禁用样式
        const thresholdGroup = input.closest('.threshold-group');
        if (thresholdGroup) {
            if (enable) {
                thresholdGroup.classList.remove('disabled');
            } else {
                thresholdGroup.classList.add('disabled');
            }
        }
    });
    
    // 启用或禁用保存按钮
    if (saveThresholdsBtn) {
        saveThresholdsBtn.disabled = !enable;
        if (enable) {
            saveThresholdsBtn.classList.remove('btn-secondary');
            saveThresholdsBtn.classList.add('btn-primary');
        } else {
            saveThresholdsBtn.classList.remove('btn-primary');
            saveThresholdsBtn.classList.add('btn-secondary');
        }
    }
}

/**
 * 应用预设模式
 */
async function applyPresetMode(preset) {
    if (!mqttConnectedStatus) {
        showAlert('MQTT服务器未连接，无法应用预设模式');
        return;
    }

    if (!deviceOnlineStatus) {
        showAlert('设备离线，无法应用预设模式');
        return;
    }

    try {
        const response = await fetch('/api/thresholds', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            },
            body: JSON.stringify({ preset })
        });

        const result = await response.json();
        
        if (result.success) {
            // 更新UI
            updatePresetButtonsUI(preset);
            showAlert(`已应用${presetModes[preset] || preset}预设`);
            // 刷新数据
            fetchData();
        } else {
            showAlert(`应用预设模式失败: ${result.message || '未知错误'}`);
        }
    } catch (error) {
        console.error('应用预设模式失败:', error);
        showAlert('应用预设模式失败，请重试');
    }
}

/**
 * 从服务器获取数据
 */
async function fetchData() {
    try {
        const response = await fetch('/api/data');
        const data = await response.json();
        
        // 检查是否有有效的传感器数据
        const hasSensorData = data.sensor_data && 
            Object.values(data.sensor_data).some(value => value !== null && value !== undefined);
        
        // 如果有有效数据，则认为设备在线
        if (hasSensorData) {
            data.mqtt_status.device_online = true;
        }
        
        updateConnectionStatus(data.mqtt_status);
        updateSensorData(data.sensor_data);
        updateDeviceStatus(data.device_status);
        
        // 当获取到新的传感器数据时，立即拉取历史数据更新图表
        fetchHistory();
        
        // 更新控制模式
        if (data.control_mode !== currentControlMode) {
            currentControlMode = data.control_mode;
            resetControlModeRadio(currentControlMode);
            
            if (currentControlMode === 'manual') {
                manualControlPanel.style.display = 'flex';
                autoControlPanel.style.display = 'none';
            } else {
                manualControlPanel.style.display = 'none';
                autoControlPanel.style.display = 'flex';
            }
        }
        
        // 更新阈值
        if (data.thresholds) {
            updateThresholdInputs(data.thresholds);
        }
        
        // 更新预设模式信息
        if (data.preset_modes) {
            presetModes = data.preset_modes;
        }
        
        // 更新当前预设模式
        if (data.current_preset && data.current_preset !== currentPreset) {
            currentPreset = data.current_preset;
            updatePresetButtonsUI(currentPreset);
        }
    } catch (error) {
        console.error('获取数据失败:', error);
        updateConnectionStatus({ connected: false, device_online: false });
    }
}

/**
 * 从服务器获取图像
 */
async function fetchImage() {
    try {
        const response = await fetch('/api/image');
        const data = await response.json();
        
        if (data.image_data) {
            // 即使设备离线，只要有图像数据就显示
            updateImageDisplay(data.image_data, data.image_info, data.image_status);
        } else {
            showNoImage();
        }
    } catch (error) {
        console.error('获取图像失败:', error);
        showNoImage();
    }
}

/**
 * 从服务器获取历史数据
 * @param {boolean} fullUpdate - 是否强制完整更新
 * @returns {Promise} - 返回Promise以便链式调用
 */
async function fetchHistory(fullUpdate = false) {
    try {
        // 如果不是强制完整更新，仅获取最近10条数据用于增量更新
        const limit = fullUpdate ? null : 10;
        const url = limit ? `/api/history?limit=${limit}` : '/api/history';
        
        const response = await fetch(url);
        const data = await response.json();
        
        // 更新图表
        Object.entries(data).forEach(([sensor, history]) => {
            if (fullUpdate) {
                // 完整更新
                updateChart(sensor, history);
            } else {
                // 增量更新 - 只添加新数据点
                updateChartIncrementally(sensor, history);
            }
        });
        
        return Promise.resolve();
    } catch (error) {
        console.error('获取历史数据失败:', error);
        return Promise.reject(error);
    }
}

/**
 * 增量更新图表，只添加新的数据点
 */
function updateChartIncrementally(sensor, recentHistory) {
    if (!sensorCharts[sensor]) return;
    
    const chart = sensorCharts[sensor];
    const currentData = chart.data.datasets[0].data;
    
    // 如果当前没有数据，直接进行完整更新
    if (currentData.length === 0) {
        updateChart(sensor, recentHistory);
        return;
    }
    
    // 获取最新数据点的时间戳
    const lastTimestampInChart = currentData.length > 0 ? 
        currentData[currentData.length - 1].x.getTime() : 0;
    
    // 处理最近的历史数据
    const newDataPoints = recentHistory
        .map(item => ({
            x: new Date(item.timestamp * 1000),
            y: item.value
        }))
        .filter(point => point.x.getTime() > lastTimestampInChart)
        .sort((a, b) => a.x - b.x);
    
    // 如果有新数据点，添加到图表并更新
    if (newDataPoints.length > 0) {
        // 添加新数据点
        chart.data.datasets[0].data = [...currentData, ...newDataPoints];
        
        // 如果数据点超过100个，移除最早的
        if (chart.data.datasets[0].data.length > 100) {
            chart.data.datasets[0].data = chart.data.datasets[0].data.slice(-100);
        }
        
        // 更新阈值线
        updateThresholdLines(sensor, chart);
        
        // 刷新图表
        chart.update();
    }
}

/**
 * 更新传感器图表数据
 */
function updateChart(sensor, history) {
    if (!sensorCharts[sensor]) return;
    
    const chart = sensorCharts[sensor];
    const chartData = history.map(item => ({
        x: new Date(item.timestamp * 1000),
        y: item.value
    }));
    
    // 按时间排序
    chartData.sort((a, b) => a.x - b.x);
    
    // 更新图表数据
    chart.data.datasets[0].data = chartData;
    
    // 更新阈值线
    updateThresholdLines(sensor, chart);
    
    // 刷新图表
    chart.update();
}

/**
 * 更新图表上的阈值线
 */
function updateThresholdLines(sensor, chart) {
    // 移除现有的阈值线
    chart.options.plugins.annotation = {
        annotations: {}
    };
    
    // 检查是否有阈值设置
    if (currentThresholds[sensor]) {
        const minValue = currentThresholds[sensor].min;
        const maxValue = currentThresholds[sensor].max;
        
        // 添加最小阈值线
        chart.options.plugins.annotation.annotations.minLine = {
            type: 'line',
            scaleID: 'y',
            value: minValue,
            borderColor: 'rgba(255, 0, 0, 0.5)',
            borderWidth: 1,
            borderDash: [5, 5],
            label: {
                content: `最小阈值: ${minValue}`,
                enabled: true,
                position: 'start'
            }
        };
        
        // 添加最大阈值线
        chart.options.plugins.annotation.annotations.maxLine = {
            type: 'line',
            scaleID: 'y',
            value: maxValue,
            borderColor: 'rgba(255, 0, 0, 0.5)',
            borderWidth: 1,
            borderDash: [5, 5],
            label: {
                content: `最大阈值: ${maxValue}`,
                enabled: true,
                position: 'end'
            }
        };
    }
}

/**
 * 发送控制命令到服务器
 */
async function sendControlCommand(device, state) {
    if (!mqttConnectedStatus) {
        showAlert('MQTT服务器未连接，无法发送控制命令');
        updateDeviceSwitch(device, 'off'); // 重置开关状态
        return;
    }

    if (!deviceOnlineStatus) {
        showAlert('设备离线，无法发送控制命令');
        updateDeviceSwitch(device, 'off'); // 重置开关状态
        return;
    }

    try {
        const command = {};
        command[device] = state;

        const response = await fetch('/api/control', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            },
            body: JSON.stringify(command)
        });

        const result = await response.json();
        
        if (!result.success) {
            showAlert(`发送命令失败: ${result.message || '未知错误'}`);
            // 重新获取设备状态以恢复UI
            fetchData();
        }
    } catch (error) {
        console.error('发送控制命令失败:', error);
        showAlert('发送控制命令失败，请重试');
        // 重新获取设备状态以恢复UI
        fetchData();
    }
}

/**
 * 更新连接状态
 */
function updateConnectionStatus(status) {
    mqttConnectedStatus = status.connected;
    deviceOnlineStatus = status.device_online;

    if (!status.connected) {
        connectionStatus.className = 'alert alert-danger';
        connectionStatus.textContent = 'MQTT服务器未连接';
    } else if (!status.device_online) {
        connectionStatus.className = 'alert alert-warning';
        connectionStatus.textContent = 'MQTT已连接，但设备离线';
    } else {
        connectionStatus.className = 'alert alert-success';
        connectionStatus.textContent = '设备在线';
    }
}

/**
 * 更新传感器数据显示
 */
function updateSensorData(data) {
    Object.entries(data).forEach(([sensor, value]) => {
        if (sensorElements[sensor]) {
            sensorElements[sensor].textContent = value;
        }
    });
}

/**
 * 更新设备状态
 */
function updateDeviceStatus(status) {
    Object.entries(status).forEach(([device, state]) => {
        updateDeviceSwitch(device, state);
    });
}

/**
 * 更新设备开关UI
 */
function updateDeviceSwitch(device, state) {
    const switchElem = deviceSwitches[device];
    if (switchElem) {
        const isOn = state.toLowerCase() === 'on';
        switchElem.checked = isOn;
        switchElem.nextElementSibling.textContent = isOn ? '开启' : '关闭';
    }
}

/**
 * 更新图像显示
 */
function updateImageDisplay(imageData, imageInfo, imageStatus) {
    if (imageData) {
        cameraImage.src = `data:image/${imageInfo.format || 'jpeg'};base64,${imageData}`;
        cameraImage.style.display = 'block';
        noImageMessage.style.display = 'none';
        
        // 显示图像信息和状态
        const timestamp = new Date(imageInfo.timestamp * 1000).toLocaleString();
        let statusText = '';
        
        // 移除旧的历史图像标记（如果有）
        const oldMark = document.querySelector('.history-image-mark');
        if (oldMark) {
            oldMark.remove();
        }
        
        // 检查是否应该显示历史图像标记
        const isLive = deviceOnlineStatus;
        const isHistoryImage = !isLive;
        
        if (isHistoryImage) {
            // 添加历史图像标记
            const historyMark = document.createElement('div');
            historyMark.className = 'history-image-mark';
            historyMark.textContent = '历史图像';
            document.getElementById('image-container').appendChild(historyMark);
            
            // 添加状态文本
            statusText = ' (历史图像)';
        }
        
        if (imageStatus && imageStatus.last_update) {
            const lastUpdateTime = new Date(imageStatus.last_update * 1000).toLocaleString();
            statusText += ` | 最后更新: ${lastUpdateTime}`;
        }
        
        // 更新图像信息显示
        const infoElement = document.getElementById('image-info');
        if (infoElement) {
            infoElement.textContent = `尺寸: ${imageInfo.width || '--'} x ${imageInfo.height || '--'} | 格式: ${imageInfo.format || '--'} | 时间戳: ${timestamp}${statusText}`;
        }
    } else {
        showNoImage();
    }
}

/**
 * 显示无图像状态
 */
function showNoImage() {
    cameraImage.style.display = 'none';
    noImageMessage.style.display = 'block';
    imageInfo.textContent = '尺寸: -- x -- | 格式: -- | 时间戳: --';
}

/**
 * 显示提示消息
 */
function showAlert(message) {
    // 简单实现，实际项目中可以使用更好的提示组件
    alert(message);
}

/**
 * 初始化所有传感器图表
 */
function initCharts() {
    const sensors = ['water', 'temp', 'humi', 'light'];
    
    sensors.forEach(sensor => {
        const ctx = document.getElementById(`${sensor}-chart`).getContext('2d');
        
        sensorCharts[sensor] = new Chart(ctx, {
            type: 'line',
            data: {
                datasets: [{
                    label: `${getSensorName(sensor)} (${sensorUnits[sensor]})`,
                    data: [],
                    borderColor: chartColors[sensor].borderColor,
                    backgroundColor: chartColors[sensor].backgroundColor,
                    borderWidth: 2,
                    pointRadius: 2,
                    pointHoverRadius: 5,
                    tension: 0.3,
                    fill: true
                }]
            },
            options: {
                responsive: true,
                maintainAspectRatio: false,
                animation: {
                    duration: 500,  // 动画持续时间更短，让更新感觉更实时
                    easing: 'easeOutQuad'
                },
                scales: {
                    x: {
                        type: 'time',
                        time: {
                            unit: 'minute',
                            displayFormats: {
                                minute: 'HH:mm'
                            },
                            tooltipFormat: 'yyyy-MM-dd HH:mm:ss'
                        },
                        title: {
                            display: true,
                            text: '时间'
                        },
                        ticks: {
                            maxRotation: 0,  // 防止标签旋转
                            autoSkip: true,  // 自动跳过标签以避免拥挤
                            maxTicksLimit: 10  // 最多显示10个标签
                        }
                    },
                    y: {
                        beginAtZero: sensor !== 'temp', // 温度可能为负值
                        title: {
                            display: true,
                            text: `${getSensorName(sensor)} (${sensorUnits[sensor]})`
                        },
                        ticks: {
                            precision: 1  // 限制小数位数，减少UI闪烁
                        }
                    }
                },
                plugins: {
                    legend: {
                        display: true,
                        position: 'top'
                    },
                    tooltip: {
                        mode: 'index',
                        intersect: false,
                        animation: {
                            duration: 100  // 快速显示提示框
                        }
                    },
                    annotation: {
                        annotations: {}
                    }
                },
                elements: {
                    line: {
                        tension: 0.3  // 使线条更平滑
                    },
                    point: {
                        radius: 2,  // 小点，减少视觉复杂度
                        hoverRadius: 5  // 悬停时变大
                    }
                },
                transitions: {
                    zoom: {
                        animation: {
                            duration: 300  // 缩放动画更快
                        }
                    }
                }
            }
        });
    });
}

/**
 * 获取传感器中文名称
 */
function getSensorName(sensor) {
    const names = {
        'water': '水位',
        'temp': '温度',
        'humi': '湿度',
        'light': '光照'
    };
    
    return names[sensor] || sensor;
} 