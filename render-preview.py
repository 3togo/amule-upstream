import ctypes as c
from pathlib import Path
from PIL import Image,ImageDraw,ImageFont,ImageOps
r=c.CDLL('librsvg-2.so.2'); a=c.CDLL('libcairo.so.2'); g=c.CDLL('libgobject-2.0.so.0')
r.rsvg_handle_new_from_file.argtypes=[c.c_char_p,c.c_void_p];r.rsvg_handle_new_from_file.restype=c.c_void_p
r.rsvg_handle_render_cairo.argtypes=[c.c_void_p,c.c_void_p];r.rsvg_handle_render_cairo.restype=c.c_int
a.cairo_image_surface_create.argtypes=[c.c_int,c.c_int,c.c_int];a.cairo_image_surface_create.restype=c.c_void_p
a.cairo_create.argtypes=[c.c_void_p];a.cairo_create.restype=c.c_void_p
a.cairo_surface_write_to_png.argtypes=[c.c_void_p,c.c_char_p];a.cairo_surface_write_to_png.restype=c.c_int
a.cairo_destroy.argtypes=[c.c_void_p];a.cairo_surface_destroy.argtypes=[c.c_void_p];g.g_object_unref.argtypes=[c.c_void_p]
root=Path(__file__).resolve().parent
labels=['aMule','eMule','MLDonkey','eDonkey Hybrid','Lphant','Shareaza','xMule'];keys=['amule','emule','mldonkey','edonkeyhybrid','lphant','shareaza','xmule']
for key in keys:
    handle=r.rsvg_handle_new_from_file(str(root/'icons'/f'{key}.svg').encode(),None);assert handle
    surface=a.cairo_image_surface_create(0,16,16);cr=a.cairo_create(surface)
    assert r.rsvg_handle_render_cairo(handle,cr)
    assert a.cairo_surface_write_to_png(surface,str(root/'icons'/f'{key}.png').encode())==0
    a.cairo_destroy(cr);a.cairo_surface_destroy(surface);g.g_object_unref(handle)
font='/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'; normal=ImageFont.truetype(font,17);small=ImageFont.truetype(font,13);big=ImageFont.truetype(font,30)
sheet=Image.new('RGB',(1100,540),'#f4f2ed');d=ImageDraw.Draw(sheet)
d.text((32,22),'Distinct shapes. Readable names. Separate status.',font=big,fill='#253347')
d.text((32,70),'Original client illustration proposal · 16 px and enlarged pixel previews · not official logos',font=normal,fill='#596476')
for row,(bg,fg) in enumerate([('#ffffff','#253347'),('#202a37','#e6edf5')]):
    y=115+row*193;d.rounded_rectangle((24,y,1076,y+175),radius=12,fill=bg)
    for i,(key,label) in enumerate(zip(keys,labels)):
        x=43+i*148;im=Image.open(root/'icons'/f'{key}.png').convert('RGBA')
        assert im.size==(16,16) and im.getextrema()[3][0]==0 and im.getbbox()
        sheet.paste(im,(x,y+43),im);large=im.resize((64,64),Image.Resampling.NEAREST);sheet.paste(large,(x+35,y+19),large)
        d.text((x,y+102),label,font=small,fill=fg)
        mono=ImageOps.grayscale(im).convert('RGBA');mono.putalpha(im.getchannel('A'));sheet.paste(mono,(x,y+137),mono)
        d.text((x+24,y+135),'greyscale',font=small,fill=fg)
d.text((32,513),'Design prototype only. Native wx rendering and user recognition have not been validated.',font=small,fill='#596476')
sheet.save(root/'contact-sheet.png')
print('Rendered seven SVG/PNG pairs; checked 16×16 dimensions and transparency; wrote contact sheet.')
