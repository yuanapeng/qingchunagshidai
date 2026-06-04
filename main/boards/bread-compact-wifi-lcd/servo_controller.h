#ifndef __SERVO_CONTROLLER_H__
#define __SERVO_CONTROLLER_H__

#include <driver/ledc.h>
#include <driver/gpio.h>
#include <driver/uart.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <functional>
#include "config.h"
#include "mcp_server.h"
#include "application.h"
#include "audio_service.h"
#include "assets/lang_config.h"
#include "led/circular_strip.h"

class ServoController
{
public:
	// 单例模式
    static ServoController& GetInstance()
	{
        static ServoController instance;
        return instance;
    }
    // Delete copy constructor and assignment operator
    ServoController(const ServoController&) = delete;
    ServoController& operator=(const ServoController&) = delete;

	// 舞蹈动作数据结构（6个舵机角度+1个延时）
	using DanceFrame = std::array<uint16_t, 7>;
	using DanceSeq = std::vector<DanceFrame>;

    // 命令类型
    enum CommandType
	{
		CMD_ORIGIN,
        CMD_SINGLE_ACTION,
		CMD_DANCE_ACTION,
		CMD_TURN_ON,
		CMD_TURN_OFF,
		CMD_SHOW_LED
    };
    
    // 命令结构
    struct McpSendFormat
	{
        CommandType type;
        std::string cmdStr;
		int times;
    };

    // 基本控制方法
    bool init();
    void initMcpTools();  // 初始化MCP工具
    void sendMcpSeq(McpSendFormat &mcpSendCmd);

private:

    // 硬件相关
	std::pair<gpio_num_t, ledc_channel_t> servoHardParam[SERVO_SUM];
    ledc_timer_t ledc_timer_;
    
    // 状态变量
    uint8_t current_angle_[SERVO_SUM];
    
    // 任务和队列
    TaskHandle_t servo_task_handle_;
    QueueHandle_t command_queue_;

    ServoController();
    ~ServoController();

    void setServoAngle(uint8_t angle, uint8_t index);
	void smoothAndSetServoAngle(DanceSeq &danceSeq);
    bool IsValidAngle(int angle) const;
    int ConstrainAngle(int angle) const;
    static void ServoTask(void* parameter);
    void ProcessCommands();
	void sendDanceAction2Servos(DanceSeq &danceSeq, int times);
	void playMusic(const std::string_view& ogg);
	void turnOn();
	void turnOff();
	void showLed();
	void uartInit();
	void scratch2RobPro();

	DanceSeq originGroup =
	{
		{{SERVO_DEFAULT_ANGLE, SERVO_DEFAULT_ANGLE, SERVO_DEFAULT_ANGLE,
		  SERVO_DEFAULT_ANGLE, SERVO_DEFAULT_ANGLE, SERVO_DEFAULT_ANGLE, 500}}
	};

	DanceSeq fangZuoShouGroup = 
	{
		{{90, 90, 90, 90, 180, 0, 200}}
	};

	DanceSeq fangYouShouGroup = 
	{
		{{90, 90, 90, 90, 180, 0, 200}}
	};

	DanceSeq fangShuangShouGroup = 
	{
		{{90, 90, 90, 90, 180, 0, 200}}
	};

	DanceSeq huiZuoShouGroup = 
	{
		{{90, 90, 90, 90, 20, 0, 200}},
		{{90, 90, 90, 90, 50, 0, 200}}
	};

	DanceSeq huiYouShouGroup = 
	{
		{{90, 90, 90, 90, 180, 160, 200}},
		{{90, 90, 90, 90, 180, 130, 200}}
	};

	DanceSeq huiShuangShouGroup = 
	{
		{{90, 90, 90, 90, 20, 160, 200}},
		{{90, 90, 90, 90, 50, 130, 200}}
	};

	DanceSeq zuoZhiTianGroup =
	{
		{{90, 170, 90, 90, 50, 0, 430}},
		{{10, 90, 90, 90, 50, 0, 430}}
	};

	DanceSeq youZhiTianGroup =
	{
		{{90, 170, 90, 90, 180, 130, 430}},
		{{10, 90, 90, 90, 180, 130, 430}},
	};

	DanceSeq zuoZhiTianManGroup =
	{
		{{90, 170, 90, 90, 50, 0, 1500}},
		{{10, 90, 90, 90, 50, 0, 1500}}
	};

	DanceSeq youZhiTianManGroup =
	{
		{{90, 170, 90, 90, 180, 130, 1500}},
		{{10, 90, 90, 90, 180, 130, 1500}},
	};
};

#endif // __SERVO_CONTROLLER_H__
