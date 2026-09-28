#!/usr/bin/env python3
"""Verify bank offsets, fallback coverage and actual bitmap bytes for UI text."""
from pathlib import Path
import re
from PIL import Image, ImageDraw, ImageFont
r = Path(__file__).resolve().parents[3]
s = (r/'app/desktop/assets/desktop_font.c').read_text()
font = ImageFont.truetype('/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc',24,index=2)
lookup = {}
banks = re.findall(r'static const uint8_t bank_(\d+)_bitmap',s)
for key in banks:
    bitmap = bytes(map(int,re.search(r'bank_'+key+r'_bitmap\[\] = \{(.*?)\};',s,re.S)[1].replace('\n','').strip(',').split(',')))
    raw = re.search(r'bank_'+key+r'_glyphs\[\] = \{(.*?)\};',s,re.S)[1]
    glyphs = [tuple(map(int,m)) for m in re.findall(r'bitmap_index=(\d+),.adv_w=(\d+),.box_w=(\d+),.box_h=(\d+),.ofs_x=(-?\d+),.ofs_y=(-?\d+)',raw)]
    offsets = list(map(int,re.search(r'bank_'+key+r'_unicode\[\] = \{(.*?)\};',s,re.S)[1].strip().split(',')))
    start = int(re.search(r'bank_'+key+r'_cmaps\[\] = \{\s*\{.range_start=(\d+)',s)[1])
    assert len(bitmap)<=524288 and len(glyphs)==len(offsets)+1
    assert offsets==sorted(set(offsets))
    for offset,g in zip(offsets,glyphs[1:]):
        index,adv,w,h,x,y=g
        assert index<2**20 and adv<2**12 and index+(w*h+1)//2<=len(bitmap)
        cp=start+offset
        assert cp not in lookup
        lookup[cp]=(bitmap[index:index+(w*h+1)//2],w,h,x,y)
    if int(key)+1<len(banks):
        assert f'.fallback=&desktop_font_bank_{int(key)+1}' in s
for char in '拍照识物返回应用中心识别提示已锁定当前画面等待电脑开始监控设置锁屏水杯键盘鼠标龟齿黑龙':
    actual,w,h,x,y=lookup[ord(char)]
    left,top,right,bottom=font.getbbox(char,anchor='ls')
    assert (w,h,x,y)==(right-left,bottom-top,left,-bottom)
    im=Image.new('L',(max(1,w),max(1,h)))
    ImageDraw.Draw(im).text((-left,-top),char,font=font,fill=255,anchor='ls')
    px=[round(v/17) for v in im.getdata()] if w*h else []
    expected=bytes((px[i]<<4)|(px[i+1] if i+1<len(px) else 0) for i in range(0,len(px),2))
    assert actual==expected,char
print(f'PASS: {len(lookup)} glyphs, {len(banks)} bounded banks, fallback links and UI glyph pixels')
