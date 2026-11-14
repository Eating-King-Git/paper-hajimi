"""Generate v9 Tianwen XML + reviewable C++ from one phrase/event catalogue.

Does not touch the user's old .hd or vendor SDK. Run after editing asr_logic.inc.
"""
import json
import xml.etree.ElementTree as ET
from pathlib import Path
from xml_blocks import node, block, value, statement, num

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT/'firmware/ASR_DeskPetFaceGame'
PHRASES = {
    1001:'来摸摸我的左脸吧！', 1002:'来摸摸我的右脸吧！',
    1010:'对啦，就是这边！', 1011:'嘿嘿，好舒服。', 1012:'收到你的温柔啦。',
    1020:'另一边哦，先把手拿开，再试一次。',
    1021:'一次选一边哦，先把两边的手都拿开。',
    1023:'不着急，我们下次再玩。', 1024:'好呀，我们下次再玩。',
    1026:'好开心！你可以说，你好可爱，或者，再来一次。', 1032:'好呀，我先休息一会儿。',
    1050:'曼波，我在呢。', 1051:'你可以说，你好可爱，或者说，陪我玩。',
    1052:'嘿嘿，被你发现啦！来摸摸我的小脸吧。',
    1053:'谢谢你，我有点害羞啦！来玩摸摸脸吧。',
    1054:'听到你夸我，开心得想转圈！来摸摸脸吧。',
    1055:'前进！', 1056:'后退！',
    1060:'我在呢！来摸摸我的左脸吧！', 1061:'我在呢！来摸摸我的右脸吧！',
}
COMMANDS = [
    ('哈吉米','WAKE'), ('前进','FORWARD'), ('后退','BACK'),
    ('你很可爱','PRAISE'), ('你好可爱','PRAISE'), ('你真可爱','PRAISE'), ('好可爱','PRAISE'),
    ('陪我玩','GAME'), ('我们玩吧','GAME'), ('摸摸游戏','GAME'),
    ('再来一次','AGAIN'), ('不玩了','EXIT'), ('停止游戏','EXIT'), ('休息吧','REST'),
]

def uart(name, tx, rx):
    # Core numeric blocks load without the optional "baud" extension.
    return value(block('serial_begin',serial_select=name,TX=tx,RX=rx),'CONTENT',num(9600))

def main():
    OUT.mkdir(parents=True,exist_ok=True)
    metadata='\n'.join('//{playid:%d,voice:%s}'%(i,t) for i,t in PHRASES.items())
    protocol=(ROOT/'firmware/DeskPetFaceGame/Protocol.h').read_text(encoding='utf-8').replace('#pragma once','')
    logic=(OUT/'asr_logic.inc').read_text(encoding='utf-8')
    events='static const char *const dpEvents[] = {'+','.join('"'+e+'"' for _,e in COMMANDS)+'};\n'
    ids='static const uint16_t dpIds[] = {'+','.join(map(str,PHRASES))+'};\n'
    code=protocol+'\n'+events+ids+logic
    (OUT/'asr_custom_code.cpp').write_text(code,encoding='utf-8')
    root=node('xml')
    raw=block('block_with_textarea', input=code); raw.set('x','20');raw.set('y','20');root.append(raw)
    setup=block('setup');setup.set('x','620');setup.set('y','20')
    statement(setup,'DO',[
        block('asr_speak',VOICE_SET='小蝶-清新女声',vol=2,speed=16),
        block('asr_welcom',NAME='我叫哈吉米'), block('asr_sleep',NAME=''),
        *[block('asr_id',ASR=phrase,leibie='唤醒词' if i==0 else '命令词',huifu='',id=i) for i,(phrase,_) in enumerate(COMMANDS)],
        *[block('asr_add_id',NAME=text,id=id) for id,text in PHRASES.items()]
    ]);root.append(setup)
    hardware=block('hardware_init');hardware.set('x','1160');hardware.set('y','20')
    statement(hardware,'DO',[
        value(block('vol_set'),'num',num(3)),
        uart('Serial','PB_5','PB_6'),uart('Serial1','PA_2','PA_3'),
        block('block_with_textarea',input='dpInit();')]);root.append(hardware)
    callback=block('procedures_defnoreturn',NAME='ASR_CODE');callback.set('x','1160');callback.set('y','650')
    statement(callback,'STACK',[
        value(block('set_state_enter_wakeup'),'num',num(30)),
        block('block_with_textarea',input='dpRecognized(snid);')]);root.append(callback)
    ET.indent(root,space='  ')
    xml=ET.tostring(root,encoding='unicode').replace('deskpet_v5_','deskpet_v9_')
    output=OUT/'哈吉米_连续互动_v9_3.hd'
    output.write_text(xml+'\n<!-- edittype="asr_pro" -->\n<!-- officialLib="[]" -->\n<!-- personalLib="[]" -->\n',encoding='utf-8')
    ET.parse(output)
    # Equivalent full-mode C++ entry for review/SDK compilation and text-mode use.
    declarations='\n'.join('//{ID:%d,keyword:"%s",ASR:"%s",ASRTO:""}'%(i,'唤醒词' if i==0 else '命令词',p) for i,(p,_) in enumerate(COMMANDS))
    full='''#include "asr.h"
#include "setup.h"
extern "C" { void * __dso_handle = 0; }
uint32_t snid;
//{speak:小蝶-清新女声,vol:2,speed:16,platform:haohaodada}
//{playid:10001,voice:我叫哈吉米}
//{playid:10002,voice:}
'''+metadata+'\n'+declarations+'\n'+code+'''
void ASR_CODE() { set_state_enter_wakeup(30000); dpRecognized(snid); }
void hardware_init() {
  vol_set(3);
  setPinFun(13,SECOND_FUNCTION); setPinFun(14,SECOND_FUNCTION); Serial.begin(9600);
  setPinFun(2,FORTH_FUNCTION); setPinFun(3,FORTH_FUNCTION); Serial1.begin(9600);
  dpInit();
  vTaskDelete(NULL);
}
void setup() {}
'''
    (OUT/'asr_full_code.cpp').write_text(full,encoding='utf-8')
    catalogue={'version':'0.9.3','protocol':'ASCII newline v9','voice':'小蝶-清新女声','phrases':PHRASES,
               'commands':[{'snid':i,'phrase':p,'event':e} for i,(p,e) in enumerate(COMMANDS)]}
    (ROOT/'interaction/phrases.v0.9.json').write_text(json.dumps(catalogue,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print('Generated v9 .hd, C++ and phrase catalogue; audio model not generated.')

if __name__=='__main__': main()
