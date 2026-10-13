from pathlib import Path
from PIL import Image, ImageDraw, ImageFont, ImageChops, ImageStat
import json, hashlib

ROOT = Path(__file__).resolve().parent.parent
QA = ROOT / 'verification'
FONT = 'C:/Windows/Fonts/segoeui.ttf'
BOLD = 'C:/Windows/Fonts/segoeuib.ttf'
def font(size, bold=False):
    return ImageFont.truetype(BOLD if bold else FONT, size)

bg = (19, 26, 39)
canvas = Image.new('RGB', (1600, 1580), bg)
d = ImageDraw.Draw(canvas)
def text(x,y,s,size=24,color=(220,228,240),bold=False):
    d.text((x,y),s,font=font(size,bold),fill=color)
def paste_fit(im,box):
    im = im.copy().convert('RGBA')
    bounds = im.getchannel('A').getbbox()
    if bounds: im = im.crop(bounds)
    im.thumbnail((box[2]-box[0],box[3]-box[1]), Image.Resampling.LANCZOS)
    x=box[0]+(box[2]-box[0]-im.width)//2
    y=box[1]+(box[3]-box[1]-im.height)//2
    canvas.paste(im,(x,y),im)

text(64,42,'CHROMALIS',22,(115,146,182),True)
text(64,78,'EXPERIENCE CRYSTALS',48,bold=True)
text(64,147,'Opaque faceted PBR  /  No textures  /  One mesh + one material per item',23,(155,173,197))
paste_fit(Image.open(ROOT/'EXP_Crystals_Hero.png'),(110,213,1490,743))
names=['Blue','Purple','Gold']
colors=[(88,187,252),(188,138,248),(244,193,87)]
tris=[32,64,96]; verts=[18,34,50]; heights=[18,21,24]
for k,name in enumerate(names):
    x=64+k*505
    text(x,778,name.upper(),28,colors[k],True)
    text(x,818,f'{tris[k]} tris   |   {verts[k]} source vertices   |   {heights[k]} cm',20)
d.line((64,865,1536,865),fill=(56,71,92),width=2)
text(64,890,'SHAPE CHECK',23,bold=True)
text(64,925,'Front / Side / Top / Game view',20,(155,173,197))
for k,name in enumerate(names):
    for j,view in enumerate(['Front','Side','Top','Game']):
        x=64+k*505+j*116
        paste_fit(Image.open(QA/f'EXP_Crystal_{name}_{view}.png'),(x,985,x+104,1145))
        text(x+12,1161,view,16,(155,173,197))
d.line((64,1206,1536,1206),fill=(56,71,92),width=2)
text(64,1230,'SMALL-SIZE CHECK',23,bold=True)
text(64,1266,'Game view at 32 px and 64 px object height (native pixels)',20,(155,173,197))
small = Image.new('RGB',(1080,500),bg)
sd=ImageDraw.Draw(small)
pixel_report=[]
for k,name in enumerate(names):
    im=Image.open(QA/f'EXP_Crystal_{name}_Game.png').convert('RGBA')
    im=im.crop(im.getchannel('A').getbbox())
    x=64+k*505
    for j,h in enumerate([32,64]):
        tile=im.resize((round(im.width*h/im.height),h),Image.Resampling.LANCZOS)
        tile.save(QA/f'EXP_Crystal_{name}_{h}px.png')
        xx=x+32+j*120
        canvas.paste(tile,(xx,1325+64-h),tile)
        text(xx-3,1411,f'{h} px',18,(170,188,208))
        small.paste(tile,(k*360+35+j*145,85+64-h),tile)
        sd.text((k*360+24+j*145,160),f'{name} {h}px',font=font(18),fill=colors[k])
        zoom=tile.resize((tile.width*3,tile.height*3),Image.Resampling.NEAREST)
        small.paste(zoom,(k*360+20+j*145,215),zoom)
        pixel_report.append({'name':name,'object_height_px':h,'width_px':tile.width})
text(64,1510,'Blender +Z up  /  glTF +Y up  /  Centered origins  /  Studio lighting only',19,(129,151,179))
canvas.save(ROOT/'EXP_Crystals_Preview.png')
small.save(QA/'SmallSize_Inspection.png')

source=Image.open(ROOT/'EXP_Crystals_Hero.png').convert('RGBA')
roundtrip=Image.open(ROOT/'EXP_Crystals_Roundtrip.png').convert('RGBA')
diff=ImageChops.difference(source,roundtrip)
report=json.loads((ROOT/'Validation.json').read_text(encoding='utf-8'))
report['render_roundtrip']={'size':list(source.size),'mean_absolute_error_rgba_8bit':ImageStat.Stat(diff).mean,'max_difference_rgba_8bit':[e[1] for e in diff.getextrema()]}
report['small_size']=pixel_report
report['files']={p.name:{'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in ROOT.iterdir() if p.suffix in ['.glb','.blend','.png']}
(ROOT/'Validation.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print(json.dumps(report['render_roundtrip']))
print('Preview and native-size inspection saved.')
