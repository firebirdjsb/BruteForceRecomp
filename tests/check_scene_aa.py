"""Real ordered NV2A scene AA must preserve later UI pixels exactly."""
from pathlib import Path
import os,subprocess,sys
exe,xbe,out=map(Path,sys.argv[1:4]);out.mkdir(parents=True,exist_ok=True)
env={k:v for k,v in os.environ.items() if not k.startswith(('RECOMP_','BF_'))}
for renderer in ['cpu','gpu','gpu-d3d12']:
    images=[]
    for state in ['off','on']:
        name=f'{renderer}-ui-aa-{state}'
        path=out/(name+'.raw')
        result=subprocess.run([str(exe),str(xbe),name,str(path)],env=env,capture_output=True,text=True,timeout=60)
        (out/(name+'.log')).write_text(result.stdout+result.stderr)
        if result.returncode:raise RuntimeError(f'{name}: {result.returncode}\n{result.stderr[-2000:]}')
        data=path.read_bytes();assert len(data)==64*64*4;images.append(data)
    off,on=images
    for y in range(8,24):
        for x in range(40,56):
            i=(y*64+x)*4
            assert off[i:i+4]==on[i:i+4],f'{renderer}: UI changed at {x},{y}'
    changed=sum(off[i:i+4]!=on[i:i+4] for i in range(0,len(off),4))
    assert changed>30,f'{renderer}: scene AA did not smooth the diagonal'
    print(f'{renderer}: {changed} scene pixels changed; all 256 UI pixels identical with repeated UI boundaries')
