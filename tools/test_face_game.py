"""Compile and run the real controller tests; no hardware or vendor SDK needed."""
import os
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def main():
    source = ROOT / 'tools/tests/interaction_test.cpp'
    compiler = shutil.which('g++') or shutil.which('clang++')
    with tempfile.TemporaryDirectory(prefix='paper-hajimi-test-') as folder:
        build = Path(folder)
        exe = build / ('controller.exe' if os.name == 'nt' else 'controller')
        if compiler:
            subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra', str(source), '-o', str(exe)], check=True)
        elif os.name == 'nt':
            vswhere = Path(os.environ.get('ProgramFiles(x86)', '')) / 'Microsoft Visual Studio/Installer/vswhere.exe'
            if not vswhere.is_file():
                raise SystemExit('Install g++/clang++ or Visual Studio C++ Build Tools.')
            vs = subprocess.check_output([str(vswhere), '-latest', '-products', '*', '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'], text=True).strip()
            if not vs:
                raise SystemExit('Visual Studio C++ tools not found.')
            vcvars = Path(vs) / 'VC/Auxiliary/Build/vcvars64.bat'
            output = subprocess.check_output(f'cmd /d /s /c ""{vcvars}" >nul && set"', text=True)
            env = {k.upper(): v for k, v in os.environ.items()}
            for line in output.splitlines():
                if '=' in line:
                    key, val = line.split('=', 1)
                    if key:
                        env[key.upper()] = val
            compiler = sorted((Path(vs) / 'VC/Tools/MSVC').glob('*/bin/Hostx64/x64/cl.exe'))[-1]
            subprocess.run([str(compiler), '/nologo', '/EHsc', '/std:c++17', '/W4', '/utf-8', str(source), '/Fe:' + str(exe), '/Fo:' + str(build / 'controller.obj')], cwd=build, env=env, check=True)
        else:
            raise SystemExit('Install g++ or clang++ first.')
        subprocess.run([str(exe)], check=True)

if __name__ == '__main__':
    main()
