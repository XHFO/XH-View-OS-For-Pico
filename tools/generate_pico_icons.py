#!/usr/bin/env python3
"""Compile the licensed 24-unit SVG icon family into allocation-free C geometry.
离线生成；固件不解析 SVG。/ Offline only; the firmware never parses SVG.
"""
from pathlib import Path
import argparse
import hashlib
import json
import math
import re
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / 'main/assets/icons'
OUTPUT = ROOT / 'main/system/pico_icons_data.h'
ICONS = [
    ('BOOK', 'book-open'), ('HOME', 'house'), ('SETTINGS', 'settings'),
    ('BACK', 'arrow-left'), ('CLOSE', 'x'), ('SEARCH', 'search'),
    ('IMPORT', 'download'), ('NOTE', 'notebook-pen'), ('MARK', 'bookmark'),
    ('LAYOUT', 'align-left'), ('CLOCK', 'clock'), ('ROTATE', 'rotate-cw'),
    ('WIFI', 'wifi'), ('LIGHT', 'lightbulb'), ('SWITCH', 'toggle-left'),
    ('FAN', 'fan'), ('THERMOMETER', 'thermometer'), ('CLIMATE', 'air-vent'),
    ('PRINTER', 'printer'), ('GAME', 'gamepad-2'), ('CHECK', 'check'),
    ('PLUS', 'plus'), ('MINUS', 'minus'), ('REFRESH', 'refresh-cw'),
    ('LOCK', 'lock-keyhole'), ('MORE', 'ellipsis'), ('BLUETOOTH', 'bluetooth'),
    ('HIGHLIGHT', 'highlighter'), ('LIST', 'list'), ('HEART', 'heart'),
    ('BATTERY', 'battery'), ('USB', 'usb'), ('SD', 'sd-card'),
    ('SLIDERS', 'sliders-horizontal'), ('PAUSE', 'pause'), ('PLAY', 'play'),
    ('POWER', 'power'), ('GRID', 'layout-grid'), ('LINK', 'link'),
    ('DROPLET', 'droplet'),
]
TOLERANCE = 0.035


def distance(point, a, b):
    dx, dy = b[0]-a[0], b[1]-a[1]
    return abs(dx*(a[1]-point[1])-(a[0]-point[0])*dy) / math.hypot(dx, dy) if dx or dy else math.dist(point, a)


def cubic(a, b, c, d, depth=0):
    if depth >= 12 or max(distance(b, a, d), distance(c, a, d)) <= TOLERANCE:
        return [(a, d)]
    mid = lambda p, q: ((p[0]+q[0])/2, (p[1]+q[1])/2)
    ab, bc, cd = mid(a, b), mid(b, c), mid(c, d)
    abc, bcd = mid(ab, bc), mid(bc, cd)
    center = mid(abc, bcd)
    return cubic(a, ab, abc, center, depth+1) + cubic(center, bcd, cd, d, depth+1)


def arc(a, rx, ry, rotation, large, sweep, b):
    # SVG endpoint-to-center algorithm, including radius correction.
    # SVG 端点圆弧按规范转中心圆弧，并修正过小的半径。
    rx, ry = abs(rx), abs(ry)
    if not rx or not ry or a == b:
        return [(a, b)]
    phi = math.radians(rotation)
    co, si = math.cos(phi), math.sin(phi)
    dx, dy = (a[0]-b[0])/2, (a[1]-b[1])/2
    xp, yp = co*dx+si*dy, -si*dx+co*dy
    scale = xp*xp/(rx*rx)+yp*yp/(ry*ry)
    if scale > 1:
        rx, ry = rx*math.sqrt(scale), ry*math.sqrt(scale)
    denominator = rx*rx*yp*yp+ry*ry*xp*xp
    factor = math.sqrt(max(0, (rx*rx*ry*ry-denominator)/denominator)) if denominator else 0
    if bool(large) == bool(sweep):
        factor = -factor
    cxp, cyp = factor*rx*yp/ry, -factor*ry*xp/rx
    cx = co*cxp-si*cyp+(a[0]+b[0])/2
    cy = si*cxp+co*cyp+(a[1]+b[1])/2
    angle = math.atan2((yp-cyp)/ry, (xp-cxp)/rx)
    end = math.atan2((-yp-cyp)/ry, (-xp-cxp)/rx)
    delta = (end-angle) % (2*math.pi)
    if not sweep and delta > 0:
        delta -= 2*math.pi
    step = 2*math.acos(max(-1, 1-TOLERANCE/max(rx, ry)))
    steps = max(1, math.ceil(abs(delta)/step))
    points = [a]
    for i in range(1, steps):
        t = angle+delta*i/steps
        points.append((cx+co*rx*math.cos(t)-si*ry*math.sin(t), cy+si*rx*math.cos(t)+co*ry*math.sin(t)))
    points.append(b)
    return list(zip(points, points[1:]))


def path(data):
    tokens = re.findall(r'[A-Za-z]|[-+]?(?:\d*\.\d+|\d+\.?\d*)(?:[eE][-+]?\d+)?', data)
    index, command, current, start, previous, control = 0, None, (0., 0.), (0., 0.), '', None
    segments = []
    counts = {'M': 2, 'L': 2, 'H': 1, 'V': 1, 'C': 6, 'S': 4, 'Q': 4, 'T': 2, 'A': 7}
    while index < len(tokens):
        if tokens[index].isalpha():
            command = tokens[index]
            index += 1
        op, relative = command.upper(), command.islower()
        if op == 'Z':
            segments.append((current, start))
            current, previous, control, command = start, op, None, None
            continue
        if op not in counts:
            raise ValueError('Unsupported SVG command: '+str(command))
        n = counts[op]
        values = list(map(float, tokens[index:index+n]))
        if len(values) != n:
            raise ValueError('Truncated SVG command: '+str(command))
        index += n
        point = lambda x, y: (x+(current[0] if relative else 0), y+(current[1] if relative else 0))
        next_control = None
        if op == 'M':
            current = start = point(*values)
            command = 'l' if relative else 'L'
        else:
            if op == 'H':
                end = (values[0]+(current[0] if relative else 0), current[1])
            elif op == 'V':
                end = (current[0], values[0]+(current[1] if relative else 0))
            else:
                end = point(*values[-2:])
            if op in ('C', 'S'):
                first = point(*values[:2]) if op == 'C' else ((2*current[0]-control[0], 2*current[1]-control[1]) if previous in ('C', 'S') else current)
                second = point(*values[-4:-2])
                segments += cubic(current, first, second, end)
                next_control = second
            elif op in ('Q', 'T'):
                q = point(*values[:2]) if op == 'Q' else ((2*current[0]-control[0], 2*current[1]-control[1]) if previous in ('Q', 'T') else current)
                b = (current[0]+2*(q[0]-current[0])/3, current[1]+2*(q[1]-current[1])/3)
                c = (end[0]+2*(q[0]-end[0])/3, end[1]+2*(q[1]-end[1])/3)
                segments += cubic(current, b, c, end)
                next_control = q
            elif op == 'A':
                segments += arc(current, *values[:5], end)
            else:
                segments.append((current, end))
            current = end
        previous, control = op, next_control
    return segments


def svg_segments(file):
    root = ET.parse(file).getroot()
    assert root.get('viewBox') == '0 0 24 24'
    assert root.get('stroke-width') == '2'
    assert root.get('stroke-linecap') == root.get('stroke-linejoin') == 'round'
    result = []
    for element in root:
        tag = element.tag.rsplit('}', 1)[-1]
        number = lambda key, default=0: float(element.get(key, default))
        if tag == 'path':
            result += path(element.get('d'))
        elif tag == 'circle':
            x, y, r = number('cx'), number('cy'), number('r')
            result += arc((x+r, y), r, r, 0, 0, 1, (x-r, y))
            result += arc((x-r, y), r, r, 0, 0, 1, (x+r, y))
        elif tag == 'line':
            result.append(((number('x1'), number('y1')), (number('x2'), number('y2'))))
        elif tag in ('polyline', 'polygon'):
            values = list(map(float, re.findall(r'[-+]?(?:\d*\.\d+|\d+)', element.get('points'))))
            points = list(zip(values[::2], values[1::2]))
            if tag == 'polygon' and points[-1] != points[0]:
                points.append(points[0])
            result += list(zip(points, points[1:]))
        elif tag == 'rect':
            x, y, w, h = number('x'), number('y'), number('width'), number('height')
            rx = min(number('rx', element.get('ry', '0')), w/2)
            ry = min(number('ry', element.get('rx', '0')), h/2)
            result += path(f'M{x+rx} {y}H{x+w-rx}A{rx} {ry} 0 0 1 {x+w} {y+ry}V{y+h-ry}A{rx} {ry} 0 0 1 {x+w-rx} {y+h}H{x+rx}A{rx} {ry} 0 0 1 {x} {y+h-ry}V{y+ry}A{rx} {ry} 0 0 1 {x+rx} {y}Z')
        else:
            raise ValueError(f'Unsupported SVG element {tag} in {file}')
    # 1/8 网格，保留圆端短点；删除量化后的重复线段。
    # Keep tiny round-cap dots, remove duplicate quantized segments.
    quantized = []
    for a, b in result:
        segment = tuple(round(v*8) for v in (*a, *b))
        if segment not in quantized and (segment[2:] + segment[:2]) not in quantized:
            quantized.append(segment)
    return quantized


def generate():
    segments, shapes, records = [], [], []
    for key, name in ICONS:
        file = ASSETS / ('pico' if key == 'SD' else 'lucide') / (name+'.svg')
        geometry = svg_segments(file)
        assert geometry and all(0 <= value <= 24*8 for line in geometry for value in line)
        shapes.append(f'    [PI_{key}] = {{{len(segments)}, {len(geometry)}}},')
        segments.extend(geometry)
        records.append({'id': 'PI_'+key, 'source': str(file.relative_to(ROOT)), 'sha256': hashlib.sha256(file.read_bytes()).hexdigest(), 'segments': len(geometry)})
    text = '''/* Generated by tools/generate_pico_icons.py; do not edit. / 生成文件，勿手改。
 * Lucide 0.468.0: ISC, copyright Lucide Contributors; Feather portions Cole Bemis.
 * Original SD-card: Apache-2.0, copyright 2026 XH_Lab.
 * Full notices: main/assets/icons/NOTICE.md and lucide/LICENSE.
 */
#pragma once
#include <stdint.h>
typedef struct { uint8_t ax, ay, bx, by; } ps_icon_segment_t;
typedef struct { uint16_t first, count; } ps_icon_shape_t;
/* 24-unit vector grid, coordinates in 1/8 units. / 24单位矢量网格，精度1/8。 */
static const ps_icon_segment_t ps_icon_segments[] = {
'''
    text += ''.join('    {'+', '.join(map(str, line))+'},\n' for line in segments)
    text += '};\nstatic const ps_icon_shape_t ps_icon_shapes[PI_COUNT] = {\n'+'\n'.join(shapes)+'\n};\n'
    return text, records, len(segments)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    text, records, count = generate()
    if args.check:
        assert OUTPUT.read_text() == text, 'Regenerate pico_icons_data.h'
    else:
        OUTPUT.write_text(text)
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps({'icons': records, 'segments': count, 'geometry_bytes': count*4+len(ICONS)*4, 'svg_runtime': False, 'tolerance_grid_units': TOLERANCE}, indent=2)+'\n')
    print(f'{len(ICONS)} icons, {count} segments, {count*4+len(ICONS)*4} geometry bytes; '+('verified' if args.check else 'generated'))


if __name__ == '__main__':
    main()
