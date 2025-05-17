#ifndef __MAIN_APP_H
#define __MAIN_APP_H

#include <stdint.h>
#include "DHT.h" // 引入DHT11传感器头文件

// C接口声明
void MainApp_Init(void);
void MainApp_RunLoop(void);
void MainApp_UpdateDeviceStatus(uint8_t fan, uint8_t exhaust, uint8_t pump);

// 新增串口接收回调函数声明
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart);

#endif /* __MAIN_APP_H */