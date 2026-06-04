---------------------------------笔记------------------------------------

一、板子的选择
    可以看到，main/boards路径下有很多类型板子的文件夹，里面放的主要是对应板子的硬件
初始化和调用函数，在实际使用时，根据用户的配置只会有一个文件夹有用。具体流程如下：
	1.用户在idf.py menuconfig里配置板子型号，产生或更新sdkconfig文件会记录配置
	2.编译工程，产生或更新build文件夹，build/config里的sdkconfig.h将sdkconfig文件里的配置转换为宏定义(未配置项为0),例如：
		#define CONFIG_BOARD_TYPE_BREAD_COMPACT_WIFI_LCD 1
	3.main/CMakelists.txt文件里根据宏定义设置板子类型BOARD_TYPE，例如：
		elseif(CONFIG_BOARD_TYPE_BREAD_COMPACT_WIFI_LCD)
    	set(BOARD_TYPE "bread-compact-wifi-lcd")
		set(BUILTIN_TEXT_FONT font_puhui_basic_16_4)
		set(BUILTIN_ICON_FONT font_awesome_16_4)
		set(DEFAULT_EMOJI_COLLECTION twemoji_32)
	4.mian/CMakelists.txt文件构建系统依BOARD_TYPE包含相应文件夹里的代码，例如：
	    file(GLOB BOARD_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/boards/${BOARD_TYPE}/*.cc
        ${CMAKE_CURRENT_SOURCE_DIR}/boards/${BOARD_TYPE}/*.c
	相应文件代码生效。注意在查看文件时，不要选择板子文件了。

	二、添加音乐
	1.将首先得到wav格式的音频文件，用scripts/文件下的python脚本转成ogg格式；
	2.将ogg文件放到main/assets/common路径下；
	3.在lang_config.h文件里添加音频对应的参数
	4.删除build文件夹，重新编译.

	三、灯光的控制
	1.在Circular_strip.h的类里定义一个全局唯一的led对象，所有控制灯的操作都在这一个对象操作，否则容易导致冲突；
	2.控制灯的程序要抛到主线程里执行，否则不会生效。
	3.小智AI源代码控制灯的代码都依赖于定时器。