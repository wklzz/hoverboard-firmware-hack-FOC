import subprocess
import os

Import("env")

def get_version():
    # 1. 优先尝试从 VERSION 文件读取
    version_file = os.path.join(os.getcwd(), "VERSION")
    base_version = "v0.0.0"
    if os.path.exists(version_file):
        with open(version_file, "r") as f:
            base_version = f.read().strip()
    
    try:
        # 2. 获取 git 哈希以区分具体构建
        # --always: 如果没有标签则使用哈希
        # --dirty: 如果代码有未提交修改，增加 -dirty 后缀
        git_info = subprocess.check_output(['git', 'describe', '--always', '--dirty']).decode('utf-8').strip()
        # 如果 git_info 本身就是 tag 且包含 base_version，直接返回
        if base_version in git_info:
            return git_info
        return f"{base_version}-{git_info}"
    except:
        return base_version

# 获取版本号
version = get_version()
print(f"\n>>> Auto-detected Firmware Version: {version} <<<\n")

# 将版本号注入编译宏
# 注意：需要转义引号
env.Append(BUILD_FLAGS=[
    f'-DFIRMWARE_VERSION=\\\"{version}\\\"'
])
