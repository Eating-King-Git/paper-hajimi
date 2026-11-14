# 全部接线：V9.3

[返回首页](../README.md) · [烧录步骤](quickstart.md)

适用板型：GOOUUU ESP32-S3-CAM V1.5＋ASRPRO V2.0。左右按**你面对玩具时**的左右。换线前拔USB并关闭舵机电源。

| 模块 | 引脚 | 连接终点 |
|---|---|---|
| OLED | VCC / GND | ESP32 3V3 / GND |
| OLED | SDA | ESP32 G41 |
| OLED | SCL | ESP32 G42 |
| 左红外 | VCC / GND | ESP32 3V3 / GND |
| 左红外 | OUT | ESP32 G21 |
| 右红外 | VCC / GND | ESP32 3V3 / GND |
| 右红外 | OUT | ESP32 G2 |
| ASRPRO | 5V / GND | ESP32 5V / GND |
| ASRPRO | PB5（TX0） | ESP32 G14（RX） |
| ASRPRO | PA3（RX1） | ESP32 G47（TX） |
| ASRPRO | PA2、PB6 | 留空；PB6与板载CH340相连 |
| 喇叭 | 正/负 | ASR SPK+ / SPK−；SPK−不是GND |
| SG90 | 信号橙/黄 | ESP32 G1 |
| SG90 | 红 | 蓝色降压板稳压5V输出 |
| SG90 | 棕 | 蓝色降压板GND |
| 蓝色降压板 | GND | ESP32 GND，共地 |
| 匹配的输入电源 | 正/负 | 蓝色降压板VIN / 输入GND |

运行时唯一USB插ESP32 **TTL**，ESP32 OTG和ASR USB空置。摄像头保持板上原连接，但程序没有启用它。电机、TB6612和HC05未接入。

## 电源要点

- 红外和OLED使用ESP32的3V3；不要把5V信号送入GPIO。
- 舵机通过独立稳压5V支路供电。蓝板5V不并到ESP32的5V/3V3，只通过GND共地。
- 蓝板绿色端子`VOUT=VIN`是输入直通，**不是5V输出**。输入9V时不能从这里给舵机供电。
- 商家标称蓝板输入6～24V、5V最大3A、3.3V最大1A，不代表实物和输入电池能长期提供这些电流，也不是每个插口各有3A。
- 作者旧件包含两节并联9V方块电池，该连接仅为历史样机记录，未验证续航或峰值供流，不作为复刻电源推荐。首次装机应确认输出电压、极性和负载时稳定性。

## 引脚速记

```text
OLED       SDA=41  SCL=42
左/右红外   OUT=21 / OUT=2
ASR -> ESP RX=14  （ASR PB5）
ESP -> ASR TX=47  （ASR PA3）
SG90       SIGNAL=1
```

不要照搬其他ESP32或ESP32-CAM板型的同名教程。当前引脚是在本项目板型资料和实际联调基础上选定的，启用摄像头、TF卡或更换板子后需要重新核对。
