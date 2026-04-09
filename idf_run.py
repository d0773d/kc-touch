import os, subprocess, sys

env = dict(os.environ)
# Remove MSys/Mingw vars
for k in ['MSYSTEM', 'MSYSTEM_CHOST', 'MSYSTEM_CARCH', 'MSYSTEM_PREFIX', 'MINGW_PREFIX', 'MINGW_CHOST']:
    env.pop(k, None)

env['IDF_PATH'] = r'C:\Users\d0773\esp\v5.5.2\esp-idf'
env['IDF_TOOLS_PATH'] = r'C:\Espressif'
env['IDF_PYTHON_ENV_PATH'] = r'C:\Espressif\python_env\idf5.5_py3.11_env'

# Add tool paths
tools_base = r'C:\Espressif\tools'
tool_paths = [
    os.path.join(tools_base, 'riscv32-esp-elf', 'esp-14.2.0_20251107', 'riscv32-esp-elf', 'bin'),
    os.path.join(tools_base, 'xtensa-esp-elf', 'esp-14.2.0_20251107', 'xtensa-esp-elf', 'bin'),
    os.path.join(tools_base, 'cmake', '3.30.2', 'bin'),
    os.path.join(tools_base, 'ninja', '1.12.1'),
    os.path.join(tools_base, 'esp32ulp-elf', '2.38_20240113', 'esp32ulp-elf', 'bin'),
    r'C:\Espressif\python_env\idf5.5_py3.11_env\Scripts',
    os.path.join(env['IDF_PATH'], 'tools'),
]
env['PATH'] = ';'.join(tool_paths) + ';' + env.get('PATH', '')

# Fix Unicode encoding issues on Windows
env['PYTHONIOENCODING'] = 'utf-8'
env['PYTHONUTF8'] = '1'

python = r'C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe'
idf_py = os.path.join(env['IDF_PATH'], 'tools', 'idf.py')

args = sys.argv[1:]
cmd = [python, idf_py] + args
result = subprocess.run(cmd, env=env, cwd=os.getcwd())
sys.exit(result.returncode)
