import os

def rename_files(directory):
    for dirpath, dirnames, filenames in os.walk(directory):
        for filename in filenames:
            if filename.endswith(".c"):
                base = os.path.splitext(filename)[0]
                os.rename(os.path.join(dirpath, filename), os.path.join(dirpath, base + ".cpp"))

# 请将下面的路径更改为你想要重命名文件的文件夹路径
directory = "./"
rename_files(directory)