"""Generate real Windows COFF archives without running the compiled image."""
from pathlib import Path
import subprocess
import sys

classifier, work, clang, lld, llvm_ar = sys.argv[1:]
work=Path(work);work.mkdir(parents=True,exist_ok=True)
src=work/'fixture.c';src.write_text('int fixture(void) { return 42; }\n')
obj=work/'fixture.obj';static=work/'fixture-static.lib';dll=work/'fixture.dll';imp=work/'fixture-import.lib'
def run(args):
    result=subprocess.run(list(map(str,args)),capture_output=True,text=True,timeout=60)
    assert result.returncode==0, f'{args}\n{result.stdout}\n{result.stderr}'
run([clang,'--target=x86_64-pc-windows-msvc','-ffreestanding','-c',src,'-o',obj])
run([llvm_ar,'rcs',static,obj])
run([lld,'-flavor','link','/dll','/noentry','/nodefaultlib','/export:fixture','/out:'+str(dll),'/implib:'+str(imp),obj])
run([classifier,static,'static'])
run([classifier,imp,'import'])
print('Real MSVC-compatible static and DLL-import archives classified')
