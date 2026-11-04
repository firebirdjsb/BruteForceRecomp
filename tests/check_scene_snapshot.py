"""Check actual ordered NV2A scene export and exclusion of later UI draws."""
from pathlib import Path
import json, os, struct, subprocess, sys

exe,xbe,out=map(Path,sys.argv[1:4])
api=sys.argv[4] if len(sys.argv)>4 else 'd3d11'
out.mkdir(parents=True,exist_ok=True)
env={k:v for k,v in os.environ.items() if not k.startswith(('BF_','RECOMP_'))}
env.update(BF_SCENE_CAPTURE_DIR=str(out.resolve()),BF_SCENE_CAPTURE_COUNT='1',BF_SCENE_CAPTURE_MIN_DEPTH_DRAWS='1')
mode='gpu-snapshot'+('-d3d12' if api=='d3d12' else '')
result=subprocess.run([str(exe),str(xbe),mode,str(out/'final.raw')],env=env,capture_output=True,text=True,timeout=60)
(out/'fixture.log').write_text(result.stdout+result.stderr)
assert result.returncode==0,(result.returncode,result.stderr[-3000:])
metadata=list(out.glob('frame-*.json'))
assert len(metadata)==1,metadata
path=metadata[0];info=json.loads(path.read_text())
assert info['width']==64 and info['height']==64 and info['depth_draws']==1
assert info['before_native_ui'] and not info['temporal_sdk_ready']
depth=struct.unpack('<4096f',path.with_suffix('.depth.f32').read_bytes())
assert abs(depth[32*64+16]-.75)<1e-6 and abs(depth[32*64+48]-.25)<1e-6
raw=path.with_suffix('.color.ppm').read_bytes()
head=b'P6\n64 64\n255\n';assert raw.startswith(head);rgb=raw[len(head):]
assert len(rgb)==64*64*3
assert rgb[(32*64+16)*3:(32*64+16)*3+3]==b'\0\0\xff'
assert rgb[(32*64+48)*3:(32*64+48)*3+3]==b'\xff\0\0'
final=(out/'final.raw').read_bytes()
assert len(final)==64*64*4 and all(p==0xff00ff00 for p in struct.unpack('<4096I',final))
print(f'{api}: actual NV2A pre-UI blue/red scene and .75/.25 device depth exported; later green UI excluded; duplicate boundary ignored')
