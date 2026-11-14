# 快速开始：只有一根USB也能烧录

[返回首页](../README.md) · [接线表](wiring.md) · [故障排查](troubleshooting.md)

## 1. 软件与版本

- Arduino IDE，安装 Espressif 的ESP32开发板包。本项目已验证 core **3.3.10**。
- 天问 Block，选 **ASRPRO 专业模式**。作者环境为2025夏季正式版SP1；软件安装、授权、联网生成资源条件以提供方为准。
- ESP32只使用开发板包自带Arduino/Wire等库，无需额外OLED或Servo库。
- 运行不依赖联网；天问首次生成语音/识别资源可能需要联网。

官方入口：[Arduino IDE](https://www.arduino.cc/en/software/)、[Espressif Arduino ESP32](https://github.com/espressif/arduino-esp32)、[天问五幺](https://twen51.com/)。厂商安装包/SDK未随仓库分发。

## 2. 先烧录ASRPRO

1. 断开USB、关闭舵机电源。拆开两板之间5V、GND、PB5→G14、G47→PA3四根互连线，断开舵机信号线。
2. USB接语音板，天问打开[哈吉米_连续互动_v9_3.hd](../firmware/ASR_DeskPetFaceGame/哈吉米_连续互动_v9_3.hd)。
3. 芯片/存储容量按自己的实物选择；板子印有V2.0并不能据此确定Flash容量。
4. **重新生成识别和语音资源，再编译下载。** 不要沿用旧缓存音频。当前14条口令、20条播报，欢迎语另算。
5. 上电可听到“我叫哈吉米”，只能证明欢迎语可播报；联动仍需后面的双向串口验证。

`.hd`是可编辑工程；`asr_full_code.cpp`是完整C++对照，`asr_custom_code.cpp`是自定义块内容。通常只需打开`.hd`。本项目保留内置数字积木，音量3、两路波特率9600，避免可选`asr_vol`/`baud`扩展缺失问题。

## 3. 烧录ESP32

1. USB改接ESP32的 **TTL** 接口。
2. 打开[DeskPetFaceGame.ino](../firmware/DeskPetFaceGame/DeskPetFaceGame.ino)，同目录`Interaction.h`、`Protocol.h`不可漏掉。
3. 选择 **ESP32S3 Dev Module**，选择当前实际串口，配置：

| 项目 | 已验证设置 |
|---|---|
| Flash Size | 16MB（本项目板型；换板先核实） |
| Partition Scheme | 3MB APP / 9MB FATFS（16MB） |
| PSRAM | Disabled（当前程序不用） |
| USB CDC On Boot | Disabled |
| USB Mode | Hardware CDC and JTAG |
| Upload Mode | UART0 / Hardware CDC |
| Flash Mode | QIO |
| Upload Speed | 115200 |

对应FQBN保存在[构建配置](../tools/esp32-fqbn.txt)，选项名称可能随IDE版本略有不同。

4. 编译并上传；遇到连接失败按[排查步骤](troubleshooting.md)确认TTL、端口与下载模式。
5. Windows构建输出使用英文目录；中文输出路径曾导致本机链接失败。Arduino IDE默认临时输出即可，CLI可用：

```powershell
arduino-cli compile --fqbn "esp32:esp32:esp32s3:CDCOnBoot=default,USBMode=hwcdc,UploadMode=default,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=disabled,FlashMode=qio,UploadSpeed=115200" --build-path "$env:TEMP/paper-hajimi-build" firmware/DeskPetFaceGame
```

## 4. 恢复连接并玩起来

断电，按[接线表](wiring.md)恢复四根互连线、红外和舵机。USB留在ESP32 TTL，ASR USB空置；舵机使用独立5V并共地。

串口监视器设 **115200**，启动应有：

```text
DeskPetFaceGame v0.9.3 GUIDED CHEEKS; needs ASR v9.3 audio resources
I2C device: 0x3C
ASR READY,9
```

OLED地址由扫描决定，可为0x3C或0x3D；`link=1`表示握手完成。依次验证：

1. 无需说话，左右手分别靠近红外，观察表情及转头。
2. 双侧无遮挡，叫“哈吉米”，按语音指定侧靠近，再拿开。
3. 它会邀请另一边；两边完成后邀请夸奖或再玩。
4. 听完再说“你好可爱”，确认随机回应和下一轮摸脸。
5. 串口发`x`停止自动转头；发`a`恢复，再用新的靠近触发。

新版本语音和ESP32必须配套。“编译成功”不是“下载成功”，欢迎语正常也不等于串口联动已通过。
