"""Validate the public paired resources. Never access private or vendor paths."""
import json
import re
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
NS = {'b': 'http://www.w3.org/1999/xhtml'}

def fields(block):
    return {f.attrib['name']: f.text or '' for f in block.findall('b:field', NS)}

def main():
    asr = ROOT / 'firmware/ASR_DeskPetFaceGame'
    tree = ET.parse(asr / '哈吉米_连续互动_v9_3.hd')
    blocks = tree.findall('.//b:block', NS)
    ids = [b.attrib['id'] for b in blocks]
    assert len(ids) == len(set(ids)), 'Duplicate block ID'
    catalog = json.loads((ROOT / 'interaction/phrases.v0.9.json').read_text(encoding='utf-8'))
    assert catalog['version'] == '0.9.3'
    commands = [fields(b) for b in blocks if b.attrib['type'] == 'asr_id']
    assert len(commands) == len(catalog['commands']) == 14
    for f, c in zip(commands, catalog['commands']):
        assert f['ASR'] == c['phrase'] and int(f['id']) == c['snid'] and f['huifu'] == ''
    audio = {f['id']: f['NAME'] for b in blocks if b.attrib['type'] == 'asr_add_id' for f in [fields(b)]}
    assert audio == catalog['phrases'] and len(audio) == 20
    raw = [fields(b)['input'] for b in blocks if b.attrib['type'] == 'block_with_textarea']
    custom = (asr / 'asr_custom_code.cpp').read_text(encoding='utf-8')
    assert custom in raw
    full = (asr / 'asr_full_code.cpp').read_text(encoding='utf-8')
    assert custom in full
    for key, phrase in audio.items():
        assert '//{playid:%s,voice:%s}' % (key, phrase) in full
    ports = [fields(b) for b in blocks if b.attrib['type'] == 'serial_begin']
    assert ports == [{'serial_select': 'Serial', 'TX': 'PB_5', 'RX': 'PB_6'}, {'serial_select': 'Serial1', 'TX': 'PA_2', 'RX': 'PA_3'}]
    assert not any(b.attrib['type'] in ('asr_vol', 'baud') for b in blocks)
    for b in blocks:
        if b.attrib['type'] in ('vol_set', 'serial_begin'):
            socket = 'num' if b.attrib['type'] == 'vol_set' else 'CONTENT'
            child = b.find(f'b:value[@name="{socket}"]/b:block', NS)
            assert child is not None and child.attrib['type'] == 'math_number'
            assert fields(child)['NUM'] == ('3' if socket == 'num' else '9600')
    source = (ROOT / 'firmware/DeskPetFaceGame/DeskPetFaceGame.ino').read_text(encoding='utf-8')
    assert 'IR_LEFT=21, IR_RIGHT=2' in source and 'ASR_RX_PIN=14, ASR_TX_PIN=47' in source
    assert 'SERVO_LOW_US = 1300' in source and 'SERVO_HIGH_US = 1700' in source
    # Check Markdown relative links, including historical sketch entry points.
    for path in ROOT.rglob('*.md'):
        if '.git' in path.parts:
            continue
        for link in re.findall(r'\]\(([^)]+)\)', path.read_text(encoding='utf-8')):
            if re.match(r'^[a-zA-Z][\w+.-]*:', link) or link.startswith('#'):
                continue
            target = link.split('#')[0]
            assert (path.parent / target).exists(), f'Broken link in {path.relative_to(ROOT)}: {link}'
    print(f'PASS: {len(blocks)} blocks, 14 commands, 20 audio resources, pins and relative links.')

if __name__ == '__main__':
    main()
