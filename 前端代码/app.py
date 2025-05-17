from flask import Flask, render_template, request, jsonify
import paho.mqtt.client as mqtt
import json
import ssl
import os
import threading
import time
import logging
import uuid
import base64
import collections

# 配置日志
logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(name)s - %(levelname)s - %(message)s')
logger = logging.getLogger(__name__)

app = Flask(__name__)

# MQTT配置
MQTT_SERVER = "n09f9099.ala.cn-hangzhou.emqxsl.cn"
MQTT_PORT = 8883
MQTT_USERNAME = "znnyclient"
MQTT_PASSWORD = "znnyclient"
MQTT_CLIENT_ID = "Flask-MQTT-Client"
MQTT_CA_CERT = "emqxsl-ca.crt"

# 全局变量存储最新数据
sensor_data = {
    "water": "0%",
    "temp": "0.0C",
    "humi": "0.0%",
    "light": "0.0lux"
}

# 保存历史数据的队列，每个传感器保存最近100条数据
sensor_history = {
    "water": collections.deque(maxlen=100),
    "temp": collections.deque(maxlen=100),
    "humi": collections.deque(maxlen=100),
    "light": collections.deque(maxlen=100)
}

device_status = {
    "fan": "off",
    "exh": "off",
    "pum": "off"
}

# 控制模式设置
control_mode = "manual"  # 初始为手动模式

# 预设模式配置
preset_modes = {
    "normal": {
        "name": "正常模式",
        "thresholds": {
            "water": {"min": 30, "max": 70},
            "temp": {"min": 20, "max": 30},
            "humi": {"min": 40, "max": 70},
            "light": {"min": 500, "max": 2000}
        }
    },
    "eco": {
        "name": "节能模式",
        "thresholds": {
            "water": {"min": 20, "max": 80},
            "temp": {"min": 18, "max": 32},
            "humi": {"min": 35, "max": 80},
            "light": {"min": 300, "max": 1500}
        }
    },
    "grow": {
        "name": "生长促进模式",
        "thresholds": {
            "water": {"min": 40, "max": 80},
            "temp": {"min": 22, "max": 28},
            "humi": {"min": 50, "max": 75},
            "light": {"min": 800, "max": 3000}
        }
    },
    "custom": {
        "name": "自定义模式",
        "thresholds": {
            "water": {"min": 30, "max": 70},
            "temp": {"min": 20, "max": 30},
            "humi": {"min": 40, "max": 70},
            "light": {"min": 500, "max": 2000}
        }
    }
}

# 当前使用的预设模式
current_preset = "normal"

# 阈值设置
thresholds = {
    "water": {"min": 30, "max": 70, "unit": "%"},
    "temp": {"min": 20, "max": 30, "unit": "C"},
    "humi": {"min": 40, "max": 70, "unit": "%"},
    "light": {"min": 500, "max": 2000, "unit": "lux"}
}

# 设备与传感器的关联规则
control_rules = {
    "fan": {"sensor": "light", "on_above": False},   # 光线低开启补光灯
    "exh": {"sensor": "humi", "on_above": True},   # 湿度高开启排气
    "pum": {"sensor": "water", "on_above": False}  # 水位低开启水泵
}

# 自动控制线程
auto_control_thread = None
auto_control_running = False

# 图像相关数据
image_info = {
    "width": 0,
    "height": 0,
    "format": "",
    "timestamp": 0
}

image_data = ""
last_image_update = 0
mqtt_connected = False
last_heartbeat = 0

# 历史图像存储路径
HISTORY_IMAGE_INFO_PATH = "last_image_info.json"
HISTORY_IMAGE_DATA_PATH = "last_image_data.txt"

# 加载历史图像
def load_history_image():
    global image_info, image_data, last_image_update
    try:
        # 加载图像信息
        if os.path.exists(HISTORY_IMAGE_INFO_PATH):
            with open(HISTORY_IMAGE_INFO_PATH, 'r') as f:
                loaded_data = json.load(f)
                image_info = loaded_data['image_info']
                last_image_update = loaded_data['last_update']
                logger.info("已加载历史图像信息")
        
        # 加载图像数据
        if os.path.exists(HISTORY_IMAGE_DATA_PATH):
            with open(HISTORY_IMAGE_DATA_PATH, 'r') as f:
                image_data = f.read()
                logger.info("已加载历史图像数据")
    except Exception as e:
        logger.error(f"加载历史图像时出错: {e}")

# 保存历史图像
def save_history_image():
    try:
        # 保存图像信息
        with open(HISTORY_IMAGE_INFO_PATH, 'w') as f:
            json.dump({
                'image_info': image_info,
                'last_update': last_image_update
            }, f)
        
        # 保存图像数据
        with open(HISTORY_IMAGE_DATA_PATH, 'w') as f:
            f.write(image_data)
        
        logger.info("已保存历史图像")
    except Exception as e:
        logger.error(f"保存历史图像时出错: {e}")

# 启动时加载历史图像
load_history_image()

# MQTT客户端回调函数
def on_connect(client, userdata, flags, reason_code, properties=None):
    global mqtt_connected
    if reason_code == 0:
        logger.info("MQTT连接成功")
        mqtt_connected = True
        # 订阅主题
        client.subscribe("esp32cam/heartbeat")
        client.subscribe("esp32cam/sensor")
        client.subscribe("esp32cam/status")
        client.subscribe("esp32cam/image/info")
        client.subscribe("esp32cam/image/data")
    else:
        logger.error(f"MQTT连接失败，返回码: {reason_code}")
        mqtt_connected = False

def on_disconnect(client, userdata, rc, properties=None):
    global mqtt_connected
    logger.info(f"MQTT断开连接，返回码: {rc}")
    mqtt_connected = False

def on_message(client, userdata, msg):
    global sensor_data, device_status, image_info, image_data, last_heartbeat, last_image_update
    
    topic = msg.topic
    payload = msg.payload.decode("utf-8")
    
    try:
        if topic == "esp32cam/heartbeat":
            logger.info("收到心跳信息")
            last_heartbeat = time.time()
        
        elif topic == "esp32cam/sensor":
            data = json.loads(payload)
            timestamp = time.time()
            sensor_data.update(data)
            
            # 添加数据到历史记录中，包含时间戳
            for key, value in data.items():
                # 提取数值部分（去除单位）
                numeric_value = extract_numeric_value(value, key)
                if numeric_value is not None:
                    sensor_history[key].append({
                        "value": numeric_value,
                        "timestamp": timestamp
                    })
            
            logger.info(f"传感器数据更新: {sensor_data}")
        
        elif topic == "esp32cam/status":
            data = json.loads(payload)
            device_status.update(data)
            logger.info(f"设备状态更新: {device_status}")
        
        elif topic == "esp32cam/image/info":
            data = json.loads(payload)
            image_info.update(data)
            logger.info(f"图像信息更新: {image_info}")
        
        elif topic == "esp32cam/image/data":
            image_data = payload
            last_image_update = time.time()
            logger.info("图像数据已更新")
            
            # 保存最新图像
            save_history_image()
    
    except Exception as e:
        logger.error(f"处理消息时出错: {e}")

# 从传感器数据中提取数值
def extract_numeric_value(value_str, sensor_type):
    try:
        if sensor_type == "water":
            return float(value_str.rstrip("%"))
        elif sensor_type == "temp":
            return float(value_str.rstrip("C"))
        elif sensor_type == "humi":
            return float(value_str.rstrip("%"))
        elif sensor_type == "light":
            return float(value_str.rstrip("lux"))
        return None
    except (ValueError, TypeError):
        logger.error(f"无法解析传感器值: {value_str}")
        return None

unique_client_id = f"{MQTT_CLIENT_ID}-{str(uuid.uuid4())[:8]}"
client = mqtt.Client(client_id=unique_client_id, callback_api_version=mqtt.CallbackAPIVersion.VERSION2)
client.username_pw_set(MQTT_USERNAME, MQTT_PASSWORD)
client.on_connect = on_connect
client.on_disconnect = on_disconnect
client.on_message = on_message

# 设置TLS/SSL
if os.path.exists(MQTT_CA_CERT):
    client.tls_set(
        ca_certs=MQTT_CA_CERT,
        cert_reqs=ssl.CERT_REQUIRED,
        tls_version=ssl.PROTOCOL_TLS,
    )
else:
    logger.warning(f"证书文件 {MQTT_CA_CERT} 不存在，将尝试使用非SSL连接")
    # 如果没有证书，尝试使用非SSL连接
    MQTT_PORT = 1883  # 使用非SSL端口

# 启动MQTT客户端
def start_mqtt_client():
    while True:
        try:
            if not mqtt_connected:
                logger.info("尝试连接MQTT服务器...")
                client.connect(MQTT_SERVER, MQTT_PORT)
                client.loop_start()
            time.sleep(5)
        except Exception as e:
            logger.error(f"MQTT连接错误: {e}")
            time.sleep(5)

# 启动MQTT客户端线程
mqtt_thread = threading.Thread(target=start_mqtt_client)
mqtt_thread.daemon = True
mqtt_thread.start()

# 自动控制功能
def auto_control_loop():
    global auto_control_running
    auto_control_running = True
    
    while auto_control_running:
        try:
            if mqtt_connected and control_mode == "auto":
                logger.info("执行自动控制逻辑...")
                
                # 解析传感器数据（移除单位后转换为数值）
                parsed_data = {}
                for sensor, value in sensor_data.items():
                    try:
                        numeric_value = float(value.rstrip(thresholds[sensor]["unit"]))
                        parsed_data[sensor] = numeric_value
                    except (ValueError, KeyError):
                        logger.warning(f"无法解析传感器 {sensor} 的值: {value}")
                
                # 根据规则和阈值自动控制设备
                commands = {}
                for device, rule in control_rules.items():
                    sensor = rule["sensor"]
                    if sensor in parsed_data:
                        sensor_value = parsed_data[sensor]
                        threshold_max = thresholds[sensor]["max"]
                        threshold_min = thresholds[sensor]["min"]
                        
                        # 判断是高于阈值开启还是低于阈值开启
                        if rule["on_above"]:
                            # 如果传感器值 > 最大阈值，开启设备；如果 < 最小阈值，关闭设备
                            if sensor_value > threshold_max and device_status[device] != "on":
                                commands[device] = "on"
                            elif sensor_value < threshold_min and device_status[device] != "off":
                                commands[device] = "off"
                        else:
                            # 如果传感器值 < 最小阈值，开启设备；如果 > 最大阈值，关闭设备
                            if sensor_value < threshold_min and device_status[device] != "on":
                                commands[device] = "on"
                            elif sensor_value > threshold_max and device_status[device] != "off":
                                commands[device] = "off"
                
                # 发送控制命令
                if commands:
                    logger.info(f"自动控制发送命令: {commands}")
                    client.publish("esp32cam/control", json.dumps(commands))
            
            time.sleep(5)  # 每5秒检查一次
            
        except Exception as e:
            logger.error(f"自动控制出错: {e}")
            time.sleep(5)

# 启动自动控制线程
auto_control_thread = threading.Thread(target=auto_control_loop)
auto_control_thread.daemon = True
auto_control_thread.start()

# 路由
@app.route('/')
def index():
    return render_template('index.html')

@app.route('/api/data')
def get_data():
    global sensor_data, device_status, mqtt_connected, last_heartbeat
    
    # 检查设备是否在线（60秒内有心跳）
    device_online = (time.time() - last_heartbeat) < 60 if last_heartbeat > 0 else False
    
    return jsonify({
        'sensor_data': sensor_data,
        'device_status': device_status,
        'mqtt_status': {
            'connected': mqtt_connected,
            'device_online': device_online
        },
        'control_mode': control_mode,
        'thresholds': thresholds,
        'current_preset': current_preset,
        'preset_modes': {k: v['name'] for k, v in preset_modes.items()}
    })

@app.route('/api/history')
def get_history():
    sensor_type = request.args.get('sensor', 'all')
    limit = request.args.get('limit', None)
    
    try:
        # 如果有limit参数，转换为整数
        if limit:
            limit = int(limit)
    except ValueError:
        limit = None
    
    if sensor_type == 'all':
        # 返回所有传感器的历史数据
        if limit:
            # 如果有limit参数，只返回最近的N条数据
            history_data = {
                sensor: list(history)[-limit:] 
                for sensor, history in sensor_history.items()
            }
        else:
            history_data = {sensor: list(history) for sensor, history in sensor_history.items()}
    elif sensor_type in sensor_history:
        # 返回特定传感器的历史数据
        if limit:
            history_data = {sensor_type: list(sensor_history[sensor_type])[-limit:]}
        else:
            history_data = {sensor_type: list(sensor_history[sensor_type])}
    else:
        return jsonify({'error': '无效的传感器类型'}), 400
    
    return jsonify(history_data)

@app.route('/api/control', methods=['POST'])
def send_control():
    if not mqtt_connected:
        return jsonify({'success': False, 'message': 'MQTT未连接'}), 503

    try:
        data = request.json
        command = {}
        
        # 检查并设置控制命令
        if 'fan' in data:
            command['fan'] = data['fan'].lower()
        if 'exh' in data:
            command['exh'] = data['exh'].lower()
        if 'pum' in data:
            command['pum'] = data['pum'].lower()
        
        if command:
            # 发送控制命令
            client.publish("esp32cam/control", json.dumps(command))
            return jsonify({'success': True})
        else:
            return jsonify({'success': False, 'message': '无效的控制命令'}), 400
    
    except Exception as e:
        logger.error(f"发送控制命令时出错: {e}")
        return jsonify({'success': False, 'message': str(e)}), 500

@app.route('/api/mode', methods=['POST'])
def set_control_mode():
    global control_mode
    
    try:
        data = request.json
        if 'mode' in data and data['mode'] in ['manual', 'auto']:
            control_mode = data['mode']
            logger.info(f"控制模式已更改为: {control_mode}")
            return jsonify({'success': True})
        else:
            return jsonify({'success': False, 'message': '无效的控制模式'}), 400
    
    except Exception as e:
        logger.error(f"设置控制模式时出错: {e}")
        return jsonify({'success': False, 'message': str(e)}), 500

@app.route('/api/thresholds', methods=['POST'])
def set_thresholds():
    global thresholds, current_preset
    
    try:
        data = request.json
        
        # 检查是否是预设模式请求
        if 'preset' in data:
            preset = data['preset']
            if preset in preset_modes:
                # 应用预设模式的阈值
                for sensor, values in preset_modes[preset]['thresholds'].items():
                    if sensor in thresholds:
                        thresholds[sensor]['min'] = values['min']
                        thresholds[sensor]['max'] = values['max']
                current_preset = preset
                logger.info(f"已应用预设模式: {preset_modes[preset]['name']}")
                return jsonify({'success': True, 'preset': preset})
            else:
                return jsonify({'success': False, 'message': '无效的预设模式'}), 400
        else:
            # 自定义阈值设置
            for sensor, values in data.items():
                if sensor in thresholds:
                    if 'min' in values:
                        thresholds[sensor]['min'] = float(values['min'])
                    if 'max' in values:
                        thresholds[sensor]['max'] = float(values['max'])
            
            # 当用户自定义设置后，不再使用预设模式
            current_preset = 'custom'
            logger.info(f"阈值设置已更新: {thresholds}")
            return jsonify({'success': True})
    
    except Exception as e:
        logger.error(f"设置阈值时出错: {e}")
        return jsonify({'success': False, 'message': str(e)}), 500

@app.route('/api/image')
def get_image():
    # 检查是否有图像数据
    has_image = image_data != "" and last_image_update > 0
    
    # 图像状态信息
    image_status = {
        "has_image": has_image,
        "last_update": last_image_update,
        "device_online": (time.time() - last_heartbeat) < 60 if last_heartbeat > 0 else False
    }
    
    return jsonify({
        'image_info': image_info,
        'image_data': image_data,
        'image_status': image_status
    })

if __name__ == '__main__':
    app.run(debug=True, host='0.0.0.0', port=5300) 