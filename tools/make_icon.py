# Rigenera l'icona di Stackly (variante "Rubino"): python3 tools/make_icon.py
import os, sys
here = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, here)
import icon3d as mk
root = os.path.dirname(here)
sizes = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256]
imgs = {s: mk.render(s, 'S3') for s in sizes}
imgs[256].save(os.path.join(root, 'res', 'stackly_256.png'))
for p in ('src/stackly.ico', 'res/stackly.ico'):
    imgs[256].save(os.path.join(root, p), format='ICO', sizes=[(s, s) for s in sizes], append_images=[imgs[s] for s in sizes[:-1]])
print('icona rigenerata')
