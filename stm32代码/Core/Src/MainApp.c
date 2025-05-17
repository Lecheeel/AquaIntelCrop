#include "MainApp.h"
#include "main.h"
#include "ssd1306.h"
#include "ssd1306_fonts.h"
#include <stdio.h>
#include <math.h>
#include <stdint.h>
#include <string.h>  // 添加string.h头文件用于字符串处理
#include "DHT.h"

// 定义USART2（如果未定义）
#ifndef USART2
#define USART2 ((USART_TypeDef *) USART2_BASE)
#endif

// 定义继电器引脚（如果未定义）
#ifndef RELAY_FAN_PIN
#define RELAY_FAN_PIN     GPIO_PIN_13  // 风扇继电器引脚(PC13)
#define RELAY_FAN_PORT    GPIOC
#define RELAY_EXHAUST_PIN GPIO_PIN_14  // 排气扇继电器引脚(PC14)
#define RELAY_EXHAUST_PORT GPIOC
#define RELAY_PUMP_PIN    GPIO_PIN_15  // 水泵继电器引脚(PC15)
#define RELAY_PUMP_PORT   GPIOC
#endif

// 定义按键引脚
#define BUTTON_FAN_PIN     GPIO_PIN_12  // 风扇控制按键(PB12)
#define BUTTON_FAN_PORT    GPIOB
#define BUTTON_EXHAUST_PIN GPIO_PIN_13  // 排气扇控制按键(PB13)
#define BUTTON_EXHAUST_PORT GPIOB
#define BUTTON_PUMP_PIN    GPIO_PIN_14  // 水泵控制按键(PB14)
#define BUTTON_PUMP_PORT   GPIOB

// 定义继电器逻辑（根据实际硬件可能需要调整）
#define RELAY_ON  GPIO_PIN_RESET  // 继电器低电平有效
#define RELAY_OFF GPIO_PIN_SET    // 继电器高电平无效

// 全局变量
static uint16_t lightValue = 0; // 光敏电阻ADC值
static uint16_t waterValue = 0; // 水位传感器ADC值
static float luxValue = 0.0f;   // 转换后的光照强度(lux)

// DHT11传感器相关变量
static DHT_sensor dht11;         // DHT11传感器对象
static float temperature = 0.0f; // 温度值
static float humidity = 0.0f;    // 湿度值
static uint8_t dht11Ready = 0;   // DHT11传感器就绪状态

static char displayBuffer[32]; // 显示缓冲区
static char txBuffer[128];     // UART发送缓冲区

// 串口相关变量
#define UART_RX_BUFFER_SIZE 256  // 增大缓冲区以处理更多数据
static uint8_t rxBuffer[UART_RX_BUFFER_SIZE];  // UART环形接收缓冲区
static volatile uint16_t rxHead = 0;    // 接收头指针
static volatile uint16_t rxTail = 0;    // 接收尾指针
static volatile uint8_t rxBusy = 0;     // 接收忙标志
static volatile uint8_t rxComplete = 0; // 接收完成标志
static uint8_t rxTempChar;              // 临时接收字符

// 定时器计数器(用于1秒定时发送)
static uint32_t sendTimer = 0;

// 按键相关变量
static uint8_t buttonFanState = 0;         // 风扇按键当前状态
static uint8_t buttonFanLastState = 0;     // 风扇按键上次状态
static uint32_t buttonFanDebounceTime = 0; // 风扇按键消抖时间

static uint8_t buttonExhaustState = 0;         // 排气扇按键当前状态
static uint8_t buttonExhaustLastState = 0;     // 排气扇按键上次状态
static uint32_t buttonExhaustDebounceTime = 0; // 排气扇按键消抖时间

static uint8_t buttonPumpState = 0;         // 水泵按键当前状态
static uint8_t buttonPumpLastState = 0;     // 水泵按键上次状态
static uint32_t buttonPumpDebounceTime = 0; // 水泵按键消抖时间

// 按键消抖时间（毫秒）
#define BUTTON_DEBOUNCE_TIME 300

// DHT11传感器状态
static uint8_t dht11_status = 0;
static uint8_t dht11_retry_count = 0;

// 外部变量
extern ADC_HandleTypeDef hadc1;
extern TIM_HandleTypeDef htim1; // 从main.h引入定时器句柄
extern UART_HandleTypeDef huart2; // 从main.h引入UART2句柄

// 添加设备状态变量
static uint8_t fanStatus = 0;     // 风扇状态 (0: 关, 1: 开)
static uint8_t exhaustStatus = 0; // 排气扇状态 (0: 关, 1: 开)
static uint8_t pumpStatus = 0;    // 水泵状态 (0: 关, 1: 开)

// 函数声明
static float ConvertToLux(uint16_t adcValue);
static uint8_t ConvertToPercent(uint16_t adcValue);
static void ReadSensors(void);
static void UpdateDisplay(void);
static void UpdateDeviceStatus(uint8_t fan, uint8_t exhaust, uint8_t pump);
static void ProcessJsonCommand(char* jsonString);
static void SendSensorData(void);
static void ParseJsonValue(char* json, const char* key, char* value, uint8_t maxLen);
static void ProcessButtonInput(void);

// 将ADC光敏电阻值转换为流明(lux)
static float ConvertToLux(uint16_t adcValue)
{
    // 光敏电阻通常是非线性的，光线越强，阻值越小，ADC读数越低
    // 反转ADC值（因为光线越亮，读数越低）
    float invertedValue = 4095.0f - adcValue;

    // 应用一个近似公式将ADC值转换为流明
    // 注意：这是一个简化的估算，需要根据具体的光敏电阻型号校准
    // 假设参数：R = 10k欧姆（暗电阻），gamma = 0.7（光敏电阻特性参数）

    // 归一化ADC值到0-1范围
    float normalizedValue = invertedValue / 4095.0f;

    // 应用幂函数模拟光敏电阻的非线性特性
    // 典型的光敏电阻流明与阻值关系是幂函数，这里使用简化公式
    const float maxLux = 10000.0f; // 最大流明值
    float lux = maxLux * pow(normalizedValue, 2.5f);

    return lux;
}

// 将ADC水位值转换为百分比
static uint8_t ConvertToPercent(uint16_t adcValue)
{
    // 将0-4095的ADC值映射到0-100%的范围
    // 使用浮点数计算确保精度，然后四舍五入到最接近的整数
    float percent = (float)adcValue / 4095.0f * 100.0f;
    return (uint8_t)(percent + 0.5f);
}

// 读取传感器数据
static void ReadSensors(void)
{
    // 配置为光线传感器通道
    ADC_ChannelConfTypeDef sConfig = {0};
    sConfig.Channel = ADC_CHANNEL_4;
    sConfig.Rank = 1;
    sConfig.SamplingTime = ADC_SAMPLETIME_55CYCLES_5;
    HAL_ADC_ConfigChannel(&hadc1, &sConfig);

    // 读取光线传感器值 (PA4 - ADC1_IN4)
    HAL_ADC_Start(&hadc1);
    HAL_ADC_PollForConversion(&hadc1, 100);
    lightValue = HAL_ADC_GetValue(&hadc1);
    HAL_ADC_Stop(&hadc1);

    // 将光敏电阻ADC值转换为流明
    luxValue = ConvertToLux(lightValue);

    // 配置为水位传感器通道
    sConfig.Channel = ADC_CHANNEL_5;
    HAL_ADC_ConfigChannel(&hadc1, &sConfig);

    // 读取水位传感器值 (PA5 - ADC1_IN5)
    HAL_ADC_Start(&hadc1);
    HAL_ADC_PollForConversion(&hadc1, 100);
    waterValue = HAL_ADC_GetValue(&hadc1);
    HAL_ADC_Stop(&hadc1);

    // 将ADC水位值转换为百分比
    waterValue = ConvertToPercent(waterValue);

    // 读取DHT11传感器数据
    DHT_data dhtData = DHT_getData(&dht11);

    // 检查数据是否有效
    if (dhtData.temp > -100.0f && dhtData.hum > -100.0f)
    {
        temperature = dhtData.temp;
        humidity = dhtData.hum;
        dht11Ready = 1;
        dht11_status = 1;
        dht11_retry_count = 0;
    }
    else
    {
        // 如果读取失败，尝试重试几次后才认为传感器故障
        dht11_retry_count++;
        if (dht11_retry_count > 5)
        {
            dht11Ready = 0;
            dht11_status = 0;
        }
    }
}

// 更新显示
static void UpdateDisplay(void)
{
    // 清屏
    ssd1306_Fill(Black);

    // 显示水位传感器值
    ssd1306_SetCursor(0, 0);
    sprintf(displayBuffer, "Water:%d%%", waterValue);
    ssd1306_WriteString(displayBuffer, Font_7x10, White);

    // 显示DHT11传感器数据
    if (dht11Ready)
    {
        // 显示温度（整数部分和一位小数）
        int tempInt = (int)temperature;
        int tempDec = (int)((temperature - tempInt) * 10);
        ssd1306_SetCursor(0, 12);
        sprintf(displayBuffer, "Temp:%d.%dC", tempInt, tempDec);
        ssd1306_WriteString(displayBuffer, Font_7x10, White);

        // 显示湿度（整数部分和一位小数）
        int humInt = (int)humidity;
        int humDec = (int)((humidity - humInt) * 10);
        ssd1306_SetCursor(0, 24);
        sprintf(displayBuffer, "Humi:%d.%d%%", humInt, humDec);
        ssd1306_WriteString(displayBuffer, Font_7x10, White);

        // 显示光线值（放在最下面）
        int luxInt = (int)luxValue;                   // 整数部分
        int luxDec = (int)((luxValue - luxInt) * 10); // 小数部分(一位小数)
        ssd1306_SetCursor(0, 36);
        sprintf(displayBuffer, "Light:%d.%dlux", luxInt, luxDec);
        ssd1306_WriteString(displayBuffer, Font_7x10, White);
    }
    else
    {
        ssd1306_SetCursor(0, 12);
        char errorMsg[] = "DHT11 Error";
        ssd1306_WriteString(errorMsg, Font_7x10, White);

        // 显示光线值（放在最下面）
        int luxInt = (int)luxValue;                   // 整数部分
        int luxDec = (int)((luxValue - luxInt) * 10); // 小数部分(一位小数)
        ssd1306_SetCursor(0, 24);
        sprintf(displayBuffer, "Light:%d.%dlux", luxInt, luxDec);
        ssd1306_WriteString(displayBuffer, Font_7x10, White);
    }

    // 右侧显示设备状态
    // 风扇状态
    ssd1306_SetCursor(75, 0);
    sprintf(displayBuffer, "Fan:%s", fanStatus ? "ON" : "OFF");
    ssd1306_WriteString(displayBuffer, Font_7x10, White);

    // 排气扇状态
    ssd1306_SetCursor(75, 12);
    sprintf(displayBuffer, "Exh:%s", exhaustStatus ? "ON" : "OFF");
    ssd1306_WriteString(displayBuffer, Font_7x10, White);

    // 水泵状态
    ssd1306_SetCursor(75, 24);
    sprintf(displayBuffer, "Pum:%s", pumpStatus ? "ON" : "OFF");
    ssd1306_WriteString(displayBuffer, Font_7x10, White);

    // 更新屏幕
    ssd1306_UpdateScreen();
}

// 更新设备状态函数
static void UpdateDeviceStatus(uint8_t fan, uint8_t exhaust, uint8_t pump)
{
    // 更新状态变量
    fanStatus = fan;
    exhaustStatus = exhaust;
    pumpStatus = pump;
    
    // 控制实际硬件状态
    
    // 风扇控制 (PC13)
    if (fan) {
        HAL_GPIO_WritePin(RELAY_FAN_PORT, RELAY_FAN_PIN, RELAY_ON);
        sprintf(txBuffer, "PC13 pin set to: %s", RELAY_ON == GPIO_PIN_RESET ? "LOW" : "HIGH");
        HAL_UART_Transmit(&huart2, (uint8_t*)txBuffer, strlen(txBuffer), 100);
        HAL_UART_Transmit(&huart2, (uint8_t*)"\r\n", 2, 10);
    } else {
        HAL_GPIO_WritePin(RELAY_FAN_PORT, RELAY_FAN_PIN, RELAY_OFF);
        sprintf(txBuffer, "PC13 pin set to: %s", RELAY_OFF == GPIO_PIN_RESET ? "LOW" : "HIGH");
        HAL_UART_Transmit(&huart2, (uint8_t*)txBuffer, strlen(txBuffer), 100);
        HAL_UART_Transmit(&huart2, (uint8_t*)"\r\n", 2, 10);
    }
    
    // 排气扇控制 (PC14)
    if (exhaust) {
        HAL_GPIO_WritePin(RELAY_EXHAUST_PORT, RELAY_EXHAUST_PIN, RELAY_ON);
        sprintf(txBuffer, "PC14 pin set to: %s", RELAY_ON == GPIO_PIN_RESET ? "LOW" : "HIGH");
        HAL_UART_Transmit(&huart2, (uint8_t*)txBuffer, strlen(txBuffer), 100);
        HAL_UART_Transmit(&huart2, (uint8_t*)"\r\n", 2, 10);
    } else {
        HAL_GPIO_WritePin(RELAY_EXHAUST_PORT, RELAY_EXHAUST_PIN, RELAY_OFF);
        sprintf(txBuffer, "PC14 pin set to: %s", RELAY_OFF == GPIO_PIN_RESET ? "LOW" : "HIGH");
        HAL_UART_Transmit(&huart2, (uint8_t*)txBuffer, strlen(txBuffer), 100);
        HAL_UART_Transmit(&huart2, (uint8_t*)"\r\n", 2, 10);
    }
    
    // 水泵控制 (PC15)
    if (pump) {
        HAL_GPIO_WritePin(RELAY_PUMP_PORT, RELAY_PUMP_PIN, RELAY_ON);
        sprintf(txBuffer, "PC15 pin set to: %s", RELAY_ON == GPIO_PIN_RESET ? "LOW" : "HIGH");
        HAL_UART_Transmit(&huart2, (uint8_t*)txBuffer, strlen(txBuffer), 100);
        HAL_UART_Transmit(&huart2, (uint8_t*)"\r\n", 2, 10);
    } else {
        HAL_GPIO_WritePin(RELAY_PUMP_PORT, RELAY_PUMP_PIN, RELAY_OFF);
        sprintf(txBuffer, "PC15 pin set to: %s", RELAY_OFF == GPIO_PIN_RESET ? "LOW" : "HIGH");
        HAL_UART_Transmit(&huart2, (uint8_t*)txBuffer, strlen(txBuffer), 100);
        HAL_UART_Transmit(&huart2, (uint8_t*)"\r\n", 2, 10);
    }
}

// 处理接收到的JSON命令
static void ProcessJsonCommand(char* jsonString)
{
    char value[10];
    uint8_t newFanStatus = fanStatus;
    uint8_t newExhaustStatus = exhaustStatus;
    uint8_t newPumpStatus = pumpStatus;
    uint8_t validData = 0;  // 数据有效性标志
    uint8_t statusChanged = 0; // 状态变化标志

    // 调试输出，显示收到的数据
    sprintf(txBuffer, "Received command(length=%d): %s", strlen(jsonString), jsonString);
    HAL_UART_Transmit(&huart2, (uint8_t*)txBuffer, strlen(txBuffer), 100);
    HAL_UART_Transmit(&huart2, (uint8_t*)"\r\n", 2, 10);

    // 清理JSON字符串，跳过前导空格和控制字符
    char* cleanJson = jsonString;
    while (*cleanJson && (*cleanJson <= 32 || *cleanJson > 126)) {
        cleanJson++;
    }

    // 确保第一个字符是 '{'
    if (*cleanJson != '{') {
        // 尝试寻找 '{' 字符
        cleanJson = strchr(jsonString, '{');
        if (cleanJson == NULL) {
            HAL_UART_Transmit(&huart2, (uint8_t*)"Error: Invalid JSON format (missing { at start)\r\n", 48, 100);
            return;
        }
    }

    // 检查是否包含有效的JSON格式（检查结尾是否有大括号）
    char* endBrace = strchr(cleanJson, '}');
    if (endBrace == NULL) {
        HAL_UART_Transmit(&huart2, (uint8_t*)"Error: Invalid JSON format (missing } at end)\r\n", 47, 100);
        return;
    }

    // 打印清理后的JSON
    sprintf(txBuffer, "Cleaned JSON: %s", cleanJson);
    HAL_UART_Transmit(&huart2, (uint8_t*)txBuffer, strlen(txBuffer), 100);
    HAL_UART_Transmit(&huart2, (uint8_t*)"\r\n", 2, 10);

    // 解析风扇状态
    ParseJsonValue(cleanJson, "fan", value, sizeof(value));
    sprintf(txBuffer, "Parse fan=%s\r\n", value);
    HAL_UART_Transmit(&huart2, (uint8_t*)txBuffer, strlen(txBuffer), 100);
    
    if (strlen(value) > 0) {
        validData = 1;  // 至少有一个有效字段
        // 清理可能的额外字符（比如多余的引号）
        char cleanValue[10];
        int i = 0, j = 0;
        
        // 复制值中的有效字符
        while (value[i] != '\0' && j < sizeof(cleanValue) - 1) {
            if (value[i] >= 'a' && value[i] <= 'z') {
                cleanValue[j++] = value[i];
            }
            i++;
        }
        cleanValue[j] = '\0';
        
        sprintf(txBuffer, "Clean value: '%s'\r\n", cleanValue);
        HAL_UART_Transmit(&huart2, (uint8_t*)txBuffer, strlen(txBuffer), 100);
        
        // 比较清理后的值
        if (strncmp(cleanValue, "on", 2) == 0) {
            if (newFanStatus != 1) {
                newFanStatus = 1;
                statusChanged = 1;
                HAL_UART_Transmit(&huart2, (uint8_t*)"Fan status will change: ON\r\n", 28, 100);
            }
        } else if (strncmp(cleanValue, "off", 3) == 0) {
            if (newFanStatus != 0) {
                newFanStatus = 0;
                statusChanged = 1;
                HAL_UART_Transmit(&huart2, (uint8_t*)"Fan status will change: OFF\r\n", 29, 100);
            }
        }
    }

    // 解析排气扇状态
    ParseJsonValue(cleanJson, "exh", value, sizeof(value));
    sprintf(txBuffer, "Parse exh=%s\r\n", value);
    HAL_UART_Transmit(&huart2, (uint8_t*)txBuffer, strlen(txBuffer), 100);
    
    if (strlen(value) > 0) {
        validData = 1;  // 至少有一个有效字段
        // 清理可能的额外字符（比如多余的引号）
        char cleanValue[10];
        int i = 0, j = 0;
        
        // 复制值中的有效字符
        while (value[i] != '\0' && j < sizeof(cleanValue) - 1) {
            if (value[i] >= 'a' && value[i] <= 'z') {
                cleanValue[j++] = value[i];
            }
            i++;
        }
        cleanValue[j] = '\0';
        
        sprintf(txBuffer, "Clean value: '%s'\r\n", cleanValue);
        HAL_UART_Transmit(&huart2, (uint8_t*)txBuffer, strlen(txBuffer), 100);
        
        // 比较清理后的值
        if (strncmp(cleanValue, "on", 2) == 0) {
            if (newExhaustStatus != 1) {
                newExhaustStatus = 1;
                statusChanged = 1;
                HAL_UART_Transmit(&huart2, (uint8_t*)"Exhaust status will change: ON\r\n", 32, 100);
            }
        } else if (strncmp(cleanValue, "off", 3) == 0) {
            if (newExhaustStatus != 0) {
                newExhaustStatus = 0;
                statusChanged = 1;
                HAL_UART_Transmit(&huart2, (uint8_t*)"Exhaust status will change: OFF\r\n", 33, 100);
            }
        }
    }

    // 解析水泵状态
    ParseJsonValue(cleanJson, "pum", value, sizeof(value));
    sprintf(txBuffer, "Parse pum=%s\r\n", value);
    HAL_UART_Transmit(&huart2, (uint8_t*)txBuffer, strlen(txBuffer), 100);
    
    if (strlen(value) > 0) {
        validData = 1;  // 至少有一个有效字段
        // 清理可能的额外字符（比如多余的引号）
        char cleanValue[10];
        int i = 0, j = 0;
        
        // 复制值中的有效字符
        while (value[i] != '\0' && j < sizeof(cleanValue) - 1) {
            if (value[i] >= 'a' && value[i] <= 'z') {
                cleanValue[j++] = value[i];
            }
            i++;
        }
        cleanValue[j] = '\0';
        
        sprintf(txBuffer, "Clean value: '%s'\r\n", cleanValue);
        HAL_UART_Transmit(&huart2, (uint8_t*)txBuffer, strlen(txBuffer), 100);
        
        // 检查是否是"n"，如果是，应该是"on"的一部分
        if (strcmp(cleanValue, "n") == 0) {
            strcpy(cleanValue, "on");
            sprintf(txBuffer, "Corrected to: '%s'\r\n", cleanValue);
            HAL_UART_Transmit(&huart2, (uint8_t*)txBuffer, strlen(txBuffer), 100);
        }
        
        // 比较清理后的值
        if (strncmp(cleanValue, "on", 2) == 0) {
            if (newPumpStatus != 1) {
                newPumpStatus = 1;
                statusChanged = 1;
                HAL_UART_Transmit(&huart2, (uint8_t*)"Pump status will change: ON\r\n", 29, 100);
            }
        } else if (strncmp(cleanValue, "off", 3) == 0) {
            if (newPumpStatus != 0) {
                newPumpStatus = 0;
                statusChanged = 1;
                HAL_UART_Transmit(&huart2, (uint8_t*)"Pump status will change: OFF\r\n", 30, 100);
            }
        }
    }

    // 处理特殊情况 - 如果找不到exh但存在缩写或部分字段
    if (strlen(value) == 0) {
        // 尝试直接在JSON中查找"on,",可能是"exh":on的情况
        if (strstr(cleanJson, ":on,") != NULL || strstr(cleanJson, ":on}") != NULL) {
            HAL_UART_Transmit(&huart2, (uint8_t*)"Found raw ':on' pattern, assuming exhaust=on\r\n", 45, 100);
            if (newExhaustStatus != 1) {
                newExhaustStatus = 1;
                statusChanged = 1;
                validData = 1;
                HAL_UART_Transmit(&huart2, (uint8_t*)"Exhaust status will change: ON\r\n", 32, 100);
            }
        }
    }

    // 检查是否有任何有效字段
    if (!validData) {
        // 没有有效字段，输出调试信息
        HAL_UART_Transmit(&huart2, (uint8_t*)"Error: No valid control fields found\r\n", 38, 100);
        return;
    }

    // 如果状态发生变化，更新设备状态
    if (statusChanged) {
        sprintf(txBuffer, "Update device status: Fan=%s, Exhaust=%s, Pump=%s", 
                newFanStatus ? "ON" : "OFF", 
                newExhaustStatus ? "ON" : "OFF", 
                newPumpStatus ? "ON" : "OFF");
        HAL_UART_Transmit(&huart2, (uint8_t*)txBuffer, strlen(txBuffer), 100);
        HAL_UART_Transmit(&huart2, (uint8_t*)"\r\n", 2, 10);
        
        // 更新设备状态
        UpdateDeviceStatus(newFanStatus, newExhaustStatus, newPumpStatus);
    } else {
        HAL_UART_Transmit(&huart2, (uint8_t*)"Device status unchanged\r\n", 25, 100);
    }
}

// 从JSON字符串中提取指定键的值
static void ParseJsonValue(char* json, const char* key, char* value, uint8_t maxLen)
{
    char searchKey[20];
    char* start;
    char* end;
    char debugMsg[64];
    
    // 初始化value为空字符串
    value[0] = '\0';

    // 构造搜索键的多种可能格式
    // 尝试搜索标准格式 "key":"value"
    sprintf(searchKey, "\"%s\":\"", key);
    HAL_UART_Transmit(&huart2, (uint8_t*)"Searching standard format\r\n", 26, 100);
    start = strstr(json, searchKey);
    
    // 如果找不到，尝试搜索缺少冒号的格式 "key""value"
    if (start == NULL) {
        sprintf(searchKey, "\"%s\"\"", key);
        HAL_UART_Transmit(&huart2, (uint8_t*)"Searching missing colon format\r\n", 32, 100);
        start = strstr(json, searchKey);
        
        if (start) {
            // 移动到值的开始位置
            start += strlen(searchKey);
        }
    } else {
        // 移动到值的开始位置
        start += strlen(searchKey);
    }
    
    // 尝试搜索没有引号的格式 "key":value 或 key:value 或 key:"value"
    if (start == NULL) {
        // 先尝试带引号的key
        sprintf(searchKey, "\"%s\":", key);
        HAL_UART_Transmit(&huart2, (uint8_t*)"Searching quoted key format\r\n", 29, 100);
        start = strstr(json, searchKey);
        
        if (start) {
            // 移动到值的开始位置
            start += strlen(searchKey);
            // 跳过可能的空格
            while (*start == ' ' || *start == '\t') {
                start++;
            }
        } else {
            // 再尝试不带引号的key
            sprintf(searchKey, "%s:", key);
            HAL_UART_Transmit(&huart2, (uint8_t*)"Searching unquoted key format\r\n", 31, 100);
            start = strstr(json, searchKey);
            
            if (start) {
                // 确保这是一个独立的key（前面是空格、逗号或大括号）
                if (start == json || start[-1] == ' ' || start[-1] == ',' || start[-1] == '{') {
                    // 移动到值的开始位置
                    start += strlen(searchKey);
                    // 跳过可能的空格
                    while (*start == ' ' || *start == '\t') {
                        start++;
                    }
                } else {
                    start = NULL; // 不是独立的key
                }
            }
        }
    }
    
    // 检查特殊情况 - 尝试查找可能缺少部分字符的key
    // 比如"um"实际上是"pum"，或"xh"实际上是"exh"
    if (start == NULL && (strcmp(key, "pum") == 0 || strcmp(key, "exh") == 0)) {
        char partialKey[10];
        strcpy(partialKey, key + 1); // 跳过第一个字符
        
        sprintf(searchKey, "\"%s\":\"", partialKey);
        sprintf(debugMsg, "Searching partial key: %s\r\n", partialKey);
        HAL_UART_Transmit(&huart2, (uint8_t*)debugMsg, strlen(debugMsg), 100);
        
        start = strstr(json, searchKey);
        if (start) {
            // 移动到值的开始位置
            start += strlen(searchKey);
        } else {
            // 尝试没有冒号的格式
            sprintf(searchKey, "\"%s\"\"", partialKey);
            start = strstr(json, searchKey);
            if (start) {
                // 移动到值的开始位置
                start += strlen(searchKey);
            }
        }
    }
    
    // 如果找到了起始位置
    if (start) {
        // 根据格式不同，查找值的结束位置
        if (*start == '\"') {
            // 如果值以引号开始，查找结束引号
            start++; // 跳过开始引号
            end = strchr(start, '\"');
        } else {
            // 如果值没有引号，查找下一个分隔符
            end = start;
            while (*end != '\0' && *end != ',' && *end != '}' && *end != ' ' && 
                   *end != '\t' && *end != '\r' && *end != '\n') {
                end++;
            }
        }
        
        if (end && end > start) {
            // 计算值的长度
            uint8_t len = end - start;
            if (len >= maxLen) {
                len = maxLen - 1;
            }
            
            // 复制值
            strncpy(value, start, len);
            value[len] = '\0';
            
            sprintf(debugMsg, "Found value for %s: %s\r\n", key, value);
            HAL_UART_Transmit(&huart2, (uint8_t*)debugMsg, strlen(debugMsg), 100);
            return;
        }
    }
    
    // 未找到键值
    sprintf(debugMsg, "Key '%s' not found or value empty\r\n", key);
    HAL_UART_Transmit(&huart2, (uint8_t*)debugMsg, strlen(debugMsg), 100);
}

// 发送传感器数据
static void SendSensorData(void)
{
    int tempInt = (int)temperature;
    int tempDec = (int)((temperature - tempInt) * 10);
    int humInt = (int)humidity;
    int humDec = (int)((humidity - humInt) * 10);
    int luxInt = (int)luxValue;
    int luxDec = (int)((luxValue - luxInt) * 10);

    // 格式化JSON数据
    sprintf(txBuffer, "{\"water\":\"%d%%\",\"temp\":\"%d.%dC\",\"humi\":\"%d.%d%%\",\"light\":\"%d.%dlux\",\"fan\":\"%s\",\"exh\":\"%s\",\"pum\":\"%s\"}",
            waterValue,
            tempInt, tempDec,
            humInt, humDec,
            luxInt, luxDec,
            fanStatus ? "on" : "off",
            exhaustStatus ? "on" : "off",
            pumpStatus ? "on" : "off");

    // 通过UART2发送数据
    HAL_UART_Transmit(&huart2, (uint8_t*)txBuffer, strlen(txBuffer), 100);
    
    // 发送换行符以便于查看
    uint8_t newline[2] = {'\r', '\n'};
    HAL_UART_Transmit(&huart2, newline, 2, 10);
}

// UART接收中断回调函数
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2) {
        // 计算下一个头指针位置
        uint16_t nextHead = (rxHead + 1) % UART_RX_BUFFER_SIZE;
        
        // 如果没有缓冲区溢出，则保存字符
        if (nextHead != rxTail) {
            // 检查接收到的字符是否是结束符(换行符或回车符)
            if (rxTempChar == '\n' || rxTempChar == '\r') {
                // 确保接收到的数据非空
                if (rxHead != rxTail) {
                    // 确保字符串正确终止
                    rxBuffer[rxHead] = '\0';
                    rxComplete = 1;  // 标记接收完成
                }
            } else if (rxTempChar >= 32 && rxTempChar <= 126) {
                // 只存储可打印字符
                rxBuffer[rxHead] = rxTempChar;
                rxHead = nextHead;
            }
        }
        
        // 继续接收下一个字符
        rxBusy = 1;
        HAL_UART_Receive_IT(huart, &rxTempChar, 1);
    }
}

// 初始化函数（C语言接口）
void MainApp_Init(void)
{
    // 配置定时器为微秒计数器
    // 假设定时器已经在main.c中初始化为72MHz/72=1MHz(即每计1次为1us)
    HAL_TIM_Base_Start(&htim1);

    // 初始化DHT11传感器
    dht11.DHT_Port = GPIOA;
    dht11.DHT_Pin = GPIO_PIN_1;
    dht11.type = DHT11;
    dht11.pullUp = GPIO_PULLUP;

    // 校准ADC以获得更准确的结果
    HAL_ADCEx_Calibration_Start(&hadc1);

    // 初始化OLED
    ssd1306_Init();

    // 清屏并显示欢迎信息
    ssd1306_Fill(Black);
    ssd1306_SetCursor(0, 0);
    char sensorTitle[] = "Sensor Data:";
    ssd1306_WriteString(sensorTitle, Font_7x10, White);
    ssd1306_SetCursor(0, 15);
    char initMsg[] = "Initializing...";
    ssd1306_WriteString(initMsg, Font_7x10, White);
    ssd1306_UpdateScreen();

    // 延时以确保OLED初始化完成和DHT11稳定
    HAL_Delay(100);
    
    // 初始化继电器状态（默认全部关闭）
    UpdateDeviceStatus(0, 0, 0);

    // 启动UART2接收(中断模式)
    rxBusy = 1;
    HAL_UART_Receive_IT(&huart2, &rxTempChar, 1);
}

// 处理按键输入
static void ProcessButtonInput(void)
{
    uint32_t currentTime = HAL_GetTick();
    uint8_t newButtonState;
    
    // 风扇按键处理
    newButtonState = HAL_GPIO_ReadPin(BUTTON_FAN_PORT, BUTTON_FAN_PIN) == GPIO_PIN_RESET ? 1 : 0;
    if (newButtonState != buttonFanLastState) {
        buttonFanDebounceTime = currentTime;
    }
    
    if ((currentTime - buttonFanDebounceTime) > BUTTON_DEBOUNCE_TIME) {
        if (newButtonState != buttonFanState) {
            buttonFanState = newButtonState;
            if (buttonFanState == 1) {
                // 按键按下，切换风扇状态
                fanStatus = !fanStatus;
                
                // 更新设备状态
                UpdateDeviceStatus(fanStatus, exhaustStatus, pumpStatus);
                
                // 调试输出
                sprintf(txBuffer, "Fan button pressed, new state: %s\r\n", fanStatus ? "ON" : "OFF");
                HAL_UART_Transmit(&huart2, (uint8_t*)txBuffer, strlen(txBuffer), 100);
            }
        }
    }
    buttonFanLastState = newButtonState;
    
    // 排气扇按键处理
    newButtonState = HAL_GPIO_ReadPin(BUTTON_EXHAUST_PORT, BUTTON_EXHAUST_PIN) == GPIO_PIN_RESET ? 1 : 0;
    if (newButtonState != buttonExhaustLastState) {
        buttonExhaustDebounceTime = currentTime;
    }
    
    if ((currentTime - buttonExhaustDebounceTime) > BUTTON_DEBOUNCE_TIME) {
        if (newButtonState != buttonExhaustState) {
            buttonExhaustState = newButtonState;
            if (buttonExhaustState == 1) {
                // 按键按下，切换排气扇状态
                exhaustStatus = !exhaustStatus;
                
                // 更新设备状态
                UpdateDeviceStatus(fanStatus, exhaustStatus, pumpStatus);
                
                // 调试输出
                sprintf(txBuffer, "Exhaust button pressed, new state: %s\r\n", exhaustStatus ? "ON" : "OFF");
                HAL_UART_Transmit(&huart2, (uint8_t*)txBuffer, strlen(txBuffer), 100);
            }
        }
    }
    buttonExhaustLastState = newButtonState;
    
    // 水泵按键处理
    newButtonState = HAL_GPIO_ReadPin(BUTTON_PUMP_PORT, BUTTON_PUMP_PIN) == GPIO_PIN_RESET ? 1 : 0;
    if (newButtonState != buttonPumpLastState) {
        buttonPumpDebounceTime = currentTime;
    }
    
    if ((currentTime - buttonPumpDebounceTime) > BUTTON_DEBOUNCE_TIME) {
        if (newButtonState != buttonPumpState) {
            buttonPumpState = newButtonState;
            if (buttonPumpState == 1) {
                // 按键按下，切换水泵状态
                pumpStatus = !pumpStatus;
                
                // 更新设备状态
                UpdateDeviceStatus(fanStatus, exhaustStatus, pumpStatus);
                
                // 调试输出
                sprintf(txBuffer, "Pump button pressed, new state: %s\r\n", pumpStatus ? "ON" : "OFF");
                HAL_UART_Transmit(&huart2, (uint8_t*)txBuffer, strlen(txBuffer), 100);
            }
        }
    }
    buttonPumpLastState = newButtonState;
}

// 主循环函数（C语言接口）
void MainApp_RunLoop(void)
{
    // 读取传感器数据
    ReadSensors();

    // 处理按键输入
    ProcessButtonInput();

    // 更新显示
    UpdateDisplay();

    // 每隔1秒发送一次传感器数据
    if (HAL_GetTick() - sendTimer >= 1000) {
        sendTimer = HAL_GetTick();
        SendSensorData();
    }

    // 处理接收到的命令
    if (rxComplete) {
        HAL_UART_Transmit(&huart2, (uint8_t*)"Start processing received command\r\n", 35, 100);
        
        // 创建临时缓冲区存储完整的消息
        char tempBuffer[UART_RX_BUFFER_SIZE];
        uint16_t i = 0;
        uint16_t currentTail = rxTail;
        
        // 从环形缓冲区中提取数据
        while (currentTail != rxHead && i < UART_RX_BUFFER_SIZE - 1) {
            tempBuffer[i++] = rxBuffer[currentTail];
            currentTail = (currentTail + 1) % UART_RX_BUFFER_SIZE;
        }
        tempBuffer[i] = '\0';  // 确保字符串正确终止
        
        // 更新尾指针，表示数据已被处理
        rxTail = rxHead;
        
        // 重置接收状态
        rxComplete = 0;
        
        // 如果UART因某种原因停止，则重新启动接收
        if (!rxBusy) {
            rxBusy = 1;
            HAL_UART_Receive_IT(&huart2, &rxTempChar, 1);
        }
        
        // 处理命令
        if (strlen(tempBuffer) > 0) {
            ProcessJsonCommand(tempBuffer);
        }
        
        HAL_UART_Transmit(&huart2, (uint8_t*)"Processing completed, ready for next command\r\n", 46, 100);
    }
}

// 设备状态更新函数（C语言接口）
void MainApp_UpdateDeviceStatus(uint8_t fan, uint8_t exhaust, uint8_t pump)
{
    UpdateDeviceStatus(fan, exhaust, pump);
}

// HAL UART错误回调函数
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2) {
        // 记录错误状态但不输出，避免过多调试信息
        rxBusy = 0;
        
        // 清除错误并重新启动接收
        HAL_UART_Receive_IT(huart, &rxTempChar, 1);
        rxBusy = 1;
    }
}