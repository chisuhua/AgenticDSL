#!/bin/bash

# 用于构建项目并生成可执行文件

set -e  # 遇到错误时退出

# 创建构建目录
rm -rf build && mkdir -p build

# 进入构建目录
cd build

# 配置项目
echo "配置项目..."
cmake .. -DCMAKE_BUILD_TYPE=Debug -DAGENTICDSL_BUILD_TESTS=ON

# 构建项目
echo "构建项目..."
make -j$(nproc)

echo "构建完成！"
