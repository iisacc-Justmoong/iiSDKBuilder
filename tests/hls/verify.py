from pathlib import Path
import argparse,subprocess,os,json
parser=argparse.ArgumentParser();parser.add_argument('--builder',required=True);parser.add_argument('--source',type=Path,required=True);parser.add_argument('--output',type=Path,required=True);parser.add_argument('--clang',required=True);parser.add_argument('--verilator',required=True);parser.add_argument('--yosys',required=True);parser.add_argument('--mode',choices=('prepare','execute','all'),default='all');args=parser.parse_args()
root=args.source;out=args.output;out.mkdir(parents=True,exist_ok=True);env=os.environ.copy()
for key in ('CPATH','CPLUS_INCLUDE_PATH','C_INCLUDE_PATH','LIBRARY_PATH','CMAKE_PREFIX_PATH','CPPFLAGS','CFLAGS','CXXFLAGS','LDFLAGS','DYLD_LIBRARY_PATH','DYLD_FRAMEWORK_PATH','DYLD_FALLBACK_LIBRARY_PATH'):env.pop(key,None)
env['PATH']=str(Path(args.verilator).parent)+os.pathsep+env['PATH'];state={}
def run(name,command):
 with (out/(name+'.log')).open('w') as f:code=subprocess.run(command,env=env,stdout=f,stderr=subprocess.STDOUT).returncode
 state[name]=code;(out/'state.json').write_text(json.dumps(state,indent=2)+'\n')
 if code:raise RuntimeError(name+' failed: '+(out/(name+'.log')).read_text(errors='replace')[-6000:])
rtl=[]
if args.mode in ('prepare','all'):
 for entry in ('crc8','signed_mix','widen','early','short_circuit','array_sum','local_array'):
  source=root/'examples/HLSHello/crc8.cpp' if entry=='crc8' else root/'tests/hls/kernels.cpp';directory=out/'generated'/entry;module='hls_'+entry
  run('synth-'+entry,[args.builder,'hls','--source',str(source),'--entry',entry,'--module',module,'--compiler',args.clang,'--output',str(directory)])
  path=directory/(module+'.sv');rtl.append(str(path))
  script=out/(entry+'.ys');script.write_text('read_verilog -sv "'+str(path)+'"\nhierarchy -check -top '+module+'\nproc\nopt\ncheck -assert\nstat\n')
  run('rtl-'+entry,[args.yosys,'-s',str(script)])
 (out/'model/build').mkdir(parents=True,exist_ok=True)
 run('verilator-build',[args.verilator,'--cc','--exe','--build','--top-module','hls_verify','--Mdir',str(out/'model/build'),'--prefix','Vhls_verify','-CFLAGS','-std=c++23 -fwrapv',str(root/'tests/hls/verify_top.sv'),*rtl,str(root/'tests/hls/verify.cpp')])
if args.mode in ('execute','all'):
 run('equivalence',[str(out/'model/build/Vhls_verify')])
 text=(out/'equivalence.log').read_text();assert 'HLS_CPP_RTL_EQUIVALENCE_OK transactions=75536 kernels=7 crc_pairs=65536' in text,text
 print(text.strip())
