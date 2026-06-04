#include "servo_controller.h"
#include <esp_log.h>
#include <cmath>

#define TAG "ServoController"

ServoController::ServoController()
{
	// 注意：屏幕背光用的ledc通道0，因此这里从1开始
	servoHardParam[0].first = SERVO_L_FOOT;
	servoHardParam[0].second = (ledc_channel_t)1;
	servoHardParam[1].first = SERVO_R_FOOT;
	servoHardParam[1].second = (ledc_channel_t)2;
	servoHardParam[2].first = SERVO_L_LEG;
	servoHardParam[2].second = (ledc_channel_t)3;
	servoHardParam[3].first = SERVO_R_LEG;
	servoHardParam[3].second = (ledc_channel_t)4;
	servoHardParam[4].first = SERVO_L_HAND;
	servoHardParam[4].second = (ledc_channel_t)5;
	servoHardParam[5].first = SERVO_R_HAND;
	servoHardParam[5].second = (ledc_channel_t)6;
	ledc_timer_ = LEDC_TIMER;

	servo_task_handle_ = nullptr;
    command_queue_ = nullptr;
}

ServoController::~ServoController()
{	
    if (servo_task_handle_ != nullptr)
	{
        vTaskDelete(servo_task_handle_);
    }
    if (command_queue_ != nullptr)
	{
        vQueueDelete(command_queue_);
    }
}

bool ServoController::init()
{
    ESP_LOGI(TAG, "初始化舵机控制器");
    
    // 配置LEDC定时器 (ESP32-S3最大支持14位分辨率)
    ledc_timer_config_t timerCfg =
	{
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_14_BIT,
        .timer_num = ledc_timer_,
        .freq_hz = LEDC_FREQ,
        .clk_cfg = LEDC_AUTO_CLK
    };
    
    esp_err_t ret = ledc_timer_config(&timerCfg);
    if (ret != ESP_OK)
	{
        ESP_LOGE(TAG, "LEDC定时器配置失败: %s", esp_err_to_name(ret));
        return false;
    }
    
	for (uint8_t i = 0; i < SERVO_SUM; i++)
	{
		// 6个舵机分别使用1——6通道
		ledc_channel_config_t chCfg = 
		{
			.gpio_num = servoHardParam[i].first,
			.speed_mode = LEDC_LOW_SPEED_MODE,
			.channel = servoHardParam[i].second,
			.intr_type = LEDC_INTR_DISABLE,
			.timer_sel = ledc_timer_,
			.duty = 0,
			.hpoint = 0
		};
		ret = ledc_channel_config(&chCfg);
		if (ret != ESP_OK)
		{
        	ESP_LOGE(TAG, "LEDC通道配置失败: %s, index: %d", esp_err_to_name(ret), i);
        	return false;
    	}
	}

    // 创建命令队列
    command_queue_ = xQueueCreate(10, sizeof(McpSendFormat));
    if (command_queue_ == nullptr)
	{
        ESP_LOGE(TAG, "创建命令队列失败");
        return false;
    }
    
    // 创建舵机控制任务
    BaseType_t task_ret = xTaskCreate(
        ServoTask,
        "ServoTask",
        4096,
        NULL,
        1,
        &servo_task_handle_
    );

    if (task_ret != pdPASS)
	{
        ESP_LOGE(TAG, "创建舵机任务失败");
        return false;
    }
    
    // 设置初始位置
	for (uint8_t i = 0; i < SERVO_SUM; i++)
	{
		setServoAngle(SERVO_DEFAULT_ANGLE, i);
		current_angle_[i] = SERVO_DEFAULT_ANGLE;
	}

	uartInit();
    ESP_LOGI(TAG, "舵机控制器初始化成功");
    return true;
}

void ServoController::initMcpTools()
{
    auto& mcp_server = McpServer::GetInstance();
    ESP_LOGI(TAG, "开始注册舵机MCP工具...");

	// 摆正
	mcp_server.AddTool("self.servo.origin",
                       "让机器人姿态摆正,所有舵机都转到90度",
                       PropertyList(),
                       [](const PropertyList& properties) -> ReturnValue
					   {
							McpSendFormat cmd = {CMD_ORIGIN, "origin", 1};
                            vTaskDelay(pdMS_TO_TICKS(2000));
							ServoController::GetInstance().sendMcpSeq(cmd);
							return "下发摆正的次数";
                       });

	// 科目三
    mcp_server.AddTool("self.servo.begin_kemu3",
                       "让机器人开始表演科目三舞蹈动作。",
                       PropertyList(),
                       [](const PropertyList& properties) -> ReturnValue
					   {
							McpSendFormat cmd = {CMD_DANCE_ACTION, "kemusan", 10};
                            ServoController::GetInstance().sendMcpSeq(cmd);
							return "下发科目三舞动动作的次数";
                       });

	// 开灯
    mcp_server.AddTool("self.led.turnOn",
                       "让机器人开灯,机器人一直亮着灯不闪烁",
                       PropertyList(),
                       [](const PropertyList& properties) -> ReturnValue
					   {
							McpSendFormat cmd = {CMD_TURN_ON, "turnOn", 1};
                            ServoController::GetInstance().sendMcpSeq(cmd);
							return "下发开灯的次数";
                       });

	// 关灯
    mcp_server.AddTool("self.led.turnOff",
                       "让机器人关灯,灯全部熄灭",
                       PropertyList(),
                       [](const PropertyList& properties) -> ReturnValue
					   {
							McpSendFormat cmd = {CMD_TURN_OFF, "ledOff", 1};
                            ServoController::GetInstance().sendMcpSeq(cmd);
							return "下发关灯的次数";
                       });

	// 灯光秀
    mcp_server.AddTool("self.led.showLed",
                       "让机器人表演动态灯光秀,跑马灯、闪烁、彩色循环",
                       PropertyList(),
                       [](const PropertyList& properties) -> ReturnValue
					   {
							McpSendFormat cmd = {CMD_SHOW_LED, "ledShow", 1};
                            ServoController::GetInstance().sendMcpSeq(cmd);
							return "下发灯光秀的次数";
                       });

    ESP_LOGI(TAG, "舵机MCP工具注册完成");
}

void ServoController::sendMcpSeq(McpSendFormat &mcpSendCmd)
{
    xQueueSend(command_queue_, &mcpSendCmd, portMAX_DELAY);
}

void ServoController::setServoAngle(uint8_t angle, uint8_t index)
{
	if (index >= SERVO_SUM)
	{
		ESP_LOGW(TAG, "设置舵机角度失败,原因:index:%d无效,请将限制在有效范围内", index);
		return;
	}

	if (angle == current_angle_[index])
	{
		return;
	}

    angle = ConstrainAngle(angle);
    // 高电平时长（500us~2500us）
	uint16_t highTime = (angle + 45) * 100 / 9;
	/* 高电平计数次数,1 << LEDC_RESOLUTION即计算2的LEDC_RESOLUTION次方，
	为最大计数次数	舵机要求PWM周期是20000us，highTime/20000为占空比 */
	uint32_t highCount = ((1ULL << LEDC_RESOLUTION) / 20000.0) * highTime;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, servoHardParam[index].second, highCount);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, servoHardParam[index].second);
    current_angle_[index] = angle;
}

void ServoController::smoothAndSetServoAngle(DanceSeq &danceSeq)
{
	for (auto seq : danceSeq)
	{
		int steps = seq[6] / 20;                    // 固定20ms一步（50Hz，舵机标准刷新率）
		if (steps < 1) steps = 1;                     // 至少1步，防止除0

		// 开始正弦平滑运动（慢启动→快中间→慢停止，最自然）
		for (int i = 0; i <= steps; i++)
		{
			// 计算进度 0~1
			float t = (float)i / steps;

			// 正弦缓动公式（机器人行业标准）
			float progress = 1.0f - cos(t * 3.14159265f / 2.0f);

			for (int j = 0; j < SERVO_SUM; j++)
			{
				int delta = seq[j] - current_angle_[j];
				// 计算当前角度
				int current_angle = current_angle_[j] + delta * progress;
				// 输出角度
				setServoAngle(current_angle, j);
				current_angle_[j] = current_angle;
			}
			// 固定20ms延时（舵机通用稳定刷新率）
			vTaskDelay(pdMS_TO_TICKS(20));
		}

		// 最后确保精准到达目标角度
		for (int i = 0; i < SERVO_SUM; i++)
		{
			setServoAngle(seq[i], i);
			current_angle_[i] = seq[i];
		}
	}
}

bool ServoController::IsValidAngle(int angle) const
{
    return angle >= SERVO_MIN_DEGREE && angle <= SERVO_MAX_DEGREE;
}

int ServoController::ConstrainAngle(int angle) const
{
    if (angle < SERVO_MIN_DEGREE) return SERVO_MIN_DEGREE;
    if (angle > SERVO_MAX_DEGREE) return SERVO_MAX_DEGREE;
    return angle;
}

void ServoController::ServoTask(void* parameter)
{
    ServoController::GetInstance().ProcessCommands();
}

void ServoController::ProcessCommands()
{
    McpSendFormat cmd;
    while (true)
	{
		// 处理scratch发来的指令
		scratch2RobPro();

        if (xQueueReceive(command_queue_, &cmd, pdMS_TO_TICKS(100)) == pdTRUE)
		{
            switch (cmd.type)
			{
				case CMD_ORIGIN:
				{
                    sendDanceAction2Servos(originGroup, cmd.times);
                    break;
				}
                case CMD_DANCE_ACTION:
				{
					playMusic(Lang::Sounds::OGG_KEMU3);
					vTaskDelay(pdMS_TO_TICKS(2500));
					sendDanceAction2Servos(fangShuangShouGroup, 1);
					sendDanceAction2Servos(huiZuoShouGroup, 8);
					sendDanceAction2Servos(fangZuoShouGroup, 1);
					sendDanceAction2Servos(huiYouShouGroup, 8);
					sendDanceAction2Servos(fangYouShouGroup, 1);
					sendDanceAction2Servos(huiShuangShouGroup, 8);
					sendDanceAction2Servos(fangShuangShouGroup, 1);
                    sendDanceAction2Servos(zuoZhiTianGroup, 10);
					sendDanceAction2Servos(youZhiTianGroup, 10);
					sendDanceAction2Servos(zuoZhiTianManGroup, 2);
					sendDanceAction2Servos(youZhiTianManGroup, 2);
					sendDanceAction2Servos(zuoZhiTianGroup, 10);
					sendDanceAction2Servos(youZhiTianGroup, 10);
                    break;
				}
				case CMD_TURN_ON:
				{
					turnOn();
					break;
				}
				case CMD_TURN_OFF:
				{
					turnOff();
					break;
				}
				case CMD_SHOW_LED:
				{
					showLed();
					break;
				}

				case CMD_SINGLE_ACTION:
					// TODO:用于控制单一关节运动的分支
					break;
				default:
					break;
            }
        }
    }
}

void ServoController::sendDanceAction2Servos(DanceSeq &danceSeq, int times)
{
	for (int i = 0; i < times; i++)
	{
		smoothAndSetServoAngle(danceSeq);
	}
	smoothAndSetServoAngle(originGroup);
}

// 播放音乐丢到主线程
void ServoController::playMusic(const std::string_view& ogg)
{
	Application::GetInstance().Schedule([&]() {
        Application::GetInstance().GetAudioService().PlaySound(ogg);
    });
}

void ServoController::turnOn()
{
	Application::GetInstance().Schedule([]() {
		CircularStrip::GetInstance().SetAllColor(StripColor(DEFAULT_BRIGHTNESS, DEFAULT_BRIGHTNESS, DEFAULT_BRIGHTNESS));
	});
}

void ServoController::turnOff()
{
	Application::GetInstance().Schedule([]() {
		CircularStrip::GetInstance().SetAllColor(StripColor(0, 0, 0));
	});
}

void ServoController::showLed()
{
	Application::GetInstance().Schedule([]() {
		CircularStrip::GetInstance().ledShow();
	});
}

void ServoController::uartInit()
{
	uart_config_t uartConfig = { 0 };
	uartConfig.baud_rate = UART_BOARD;
	uartConfig.data_bits = UART_DATA_8_BITS;
	uartConfig.parity = UART_PARITY_DISABLE;
	uartConfig.stop_bits = UART_STOP_BITS_1;
	uartConfig.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
	uartConfig.rx_flow_ctrl_thresh = 122;
	uartConfig.source_clk = UART_SCLK_APB;
	uart_param_config(UART_NUM, &uartConfig);

	uart_set_pin(UART_NUM, UART_TX, UART_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
	uart_driver_install(UART_NUM, BUF_SIZE, BUF_SIZE, 20, NULL, 0);
}

// 协议如下：
// std::string setLedCmd = "[deng-0-32-32-32]";(index-r-g-b)
// std::string setMotorCmd = "[dianji-90-90-90-90-90-90-200]";(servo1-servo2-servo3-servo4-servo5-servo6-delay)
void ServoController::scratch2RobPro()
{
	char getChar = ' ';
	std::string getString = " ";
	while (uart_read_bytes(UART_NUM, &getChar, 1, 100) > 0)
	{
		// 接收字符串时，会出现自动在后面加\r\n的情况，需要过滤掉
		if (getChar == '\r' || getChar == '\n')
		{
			continue;
		}

		getString += getChar;
		if (getString.size() >= BUF_SIZE - 1 || getChar == ']')
		{
			break;
		}
	}

	if ((getString.find("deng")) != std::string::npos)
	{
		uint8_t ledColor[4] = { 0 };
		for (int i = 0; i < 4; i++)
		{
			// 读index和rgb
			if ((getString.find("-")) != std::string::npos)
			{
				getString = getString.substr(getString.find("-") + 1);
			}
			else
			{
				ESP_LOGW(TAG, "未找到deng的第一个-");
				return;
			}
			ledColor[i] = stoi(getString);
			ESP_LOGI(TAG, "\nyp: %d ,\n", ledColor[i]);
			Application::GetInstance().Schedule([=]() {
			CircularStrip::GetInstance().SetSingleColor(ledColor[0], StripColor(ledColor[1], ledColor[2], ledColor[3]));
			vTaskDelay(pdMS_TO_TICKS(50));
	});
		}
	}
	else if ((getString.find("dianji")) != std::string::npos)
	{
		DanceFrame danceFrame;
		DanceSeq danceSeq;
		// 读舵机角度和延时
		for (int i = 0; i < SERVO_SUM + 1; i++)
		{
			if ((getString.find("-")) != std::string::npos)
			{
				getString = getString.substr(getString.find("-") + 1);
				danceFrame[i] = stoi(getString);
				ESP_LOGW(TAG, "%d, ", danceFrame[i]);
			}
			else
			{
				ESP_LOGW(TAG, "未找到电机的第%d个-", i);
				return;
			}
		}
		danceSeq.push_back(danceFrame);
		smoothAndSetServoAngle(danceSeq);
	}
}