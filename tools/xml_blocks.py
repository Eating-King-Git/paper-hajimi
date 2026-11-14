"""Build an editable Tianwen ASRPRO project from installed block schemas.

Only writes the new project; user originals are read-only.
"""
from pathlib import Path
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
NS = 'http://www.w3.org/1999/xhtml'
ET.register_namespace('', NS)
counter = 0


def node(tag, attrs=None, text=None):
    e = ET.Element('{' + NS + '}' + tag, attrs or {})
    e.text = text
    return e


def block(kind, **fields):
    global counter
    counter += 1
    e = node('block', {'type': kind, 'id': 'deskpet_v5_' + str(counter)})
    for k, v in fields.items():
        e.append(node('field', {'name': k}, str(v)))
    return e


def value(parent, name, child):
    e = node('value', {'name': name})
    e.append(child)
    parent.append(e)
    return parent


def chain(items):
    for a, b in zip(items, items[1:]):
        n = node('next')
        n.append(b)
        a.append(n)
    return items[0]


def statement(parent, name, items):
    e = node('statement', {'name': name})
    e.append(chain(items))
    parent.append(e)
    return parent


def num(n):
    return block('math_number', NUM=n)

