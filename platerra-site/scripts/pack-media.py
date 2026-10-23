from pathlib import Path
import json, hashlib
from PIL import Image

root=Path(__file__).resolve().parents[1]
items={'hmi-retouched':'hmi-panel','io24-retouched':'io24-board','industrial-frame':'industrial-cabinet','locker-frame':'terminal-locker','vending-retouched':'vending-mechanism'}
rows=[]
for source,name in items.items():
    p=root/'materials/processed'/f'{source}.png'
    if not p.exists():continue
    sizes={}
    for suffix,width in [('480',480),('960',960),('full',1280)]:
        dest=root/'assets'/f'{name}-{suffix}.webp'
        # Только экспорт в web-размер/формат; без ретуши и изменения содержания.
        with Image.open(p) as im:
            im=im.convert('RGB')
            if im.width>width:
                im=im.resize((width,round(im.height*width/im.width)),Image.Resampling.LANCZOS)
            im.save(dest,format='WEBP',quality=86 if suffix=='full' else 82,method=6)
        sizes[suffix]=dest.stat().st_size
    rows.append({'name':name,'processed_source':str(p.relative_to(root)),'processed_sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'webp_bytes':sizes})
(root/'materials/media-output-manifest.json').write_text(json.dumps(rows,indent=2),encoding='utf-8')
print(json.dumps(rows,indent=2))
