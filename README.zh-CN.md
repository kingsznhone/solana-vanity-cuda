# Solana Vanity CUDA

[English](README.md) | **中文**

基于 CUDA 加速的 Solana 靓号地址搜索器，采用高度激进优化的 Ed25519 GPU 搜索流水线。本项目用于暴力生成 Solana 公钥，直到其 Base58 表示匹配一个或多个固定前缀。

实现针对搜索路径进行了深度优化：使用 skip-zero 技术、固定位置的 16 位预计算表、GPU 端前缀范围匹配，以及寄存器使用量、线程块大小和每次 kernel 启动工作量等底层 CUDA 调优控制。项目不会逐字符比较字符串，而是将目标前缀转换成前导数值范围，让 GPU 在搜索过程中执行紧凑的范围判断。

这种优化策略具有明确的专用性。**不支持后缀搜索。** 当前只支持固定的 Base58 前缀搜索。

## 已测试性能

本搜索器已经在 NVIDIA RTX 3070 Ti 和 RTX 5090 上测试：

- RTX 3070 Ti：约 **30 MH/s**
- RTX 5090：约 **220 MH/s**

在这些测试中，吞吐量达到其他 GPU 靓号搜索器的 **200% 以上**。实际性能会受到 CUDA Toolkit、驱动版本、显卡频率、温度限制、编译参数以及可见 GPU 数量等因素影响。

进一步的多平台调优测试表明，`r128*b512` 组合在多个 GPU 平台上取得了最好的综合性能：

- `r128`：`VANITY_MAX_REGISTER_COUNT=128`
- `b512`：`VANITY_THREADS_PER_BLOCK=512`

这也是项目默认的编译配置，适合作为基准测试起点。不过，不同显卡仍可能通过本地调参获得额外收益。

## 环境要求

- 安装 NVIDIA 驱动的 Linux 系统
- 支持 CUDA 的 NVIDIA GPU
- CUDA Toolkit，包括 `nvcc`
- CMake 3.22 或更高版本
- 支持 C17/C++20 的主机编译器
- OpenSSL 开发文件（Debian/Ubuntu 上通常为 `libssl-dev`）
- `nvidia-smi` 位于 `PATH` 中

在 CMake 3.24 及更高版本中，启动脚本会自动使用本机 CUDA 架构。对于更早版本，脚本会通过 `nvidia-smi` 查询第一块可见 GPU。也可以通过 `CUDA_ARCHITECTURES` 手动覆盖该行为。

## 编译

仓库提供了统一的构建脚本，会自动检测 CUDA 并集中管理构建配置：

```bash
bash ./run build
```

主程序生成于：

```text
build/release/solana-vanity-cuda
```

可以指定 CUDA 编译器、架构、构建目录和并行编译数量：

```bash
CUDACXX=/usr/local/cuda/bin/nvcc \
CUDA_ARCHITECTURES=89 \
BUILD_DIR=build \
BUILD_JOBS=16 \
bash ./run build
```

## 重要编译参数

以下环境变量会传递给 CMake：

| 变量 | 可选值 | 默认值 | 说明 |
| --- | --- | --- | --- |
| `CUDA_ARCHITECTURES` | CUDA 架构或 `native` | `native` | 目标 GPU 架构，例如 RTX 3070 Ti 使用 `89`，RTX 5090 使用 `120`。 |
| `VANITY_MAX_REGISTER_COUNT` | `auto` 或 `128` | `128` | CUDA 寄存器上限。`auto` 启用 launch bounds，`128` 使用 `--maxrregcount=128`。 |
| `VANITY_THREADS_PER_BLOCK` | `256` 或 `512` | `512` | 每个线程块的 CUDA 线程数。 |
| `VANITY_ATTEMPTS_PER_EXECUTION` | 正整数 | `10000` | 每个线程在一次 kernel 启动中处理的候选密钥数量。 |
| `BUILD_TYPE` | CMake 构建类型 | `Release` | 启动脚本使用的构建配置。 |
| `BUILD_DIR` | 目录路径 | `./build` | CMake 构建目录。 |
| `BUILD_JOBS` | 正整数 | 自动检测 CPU 数量 | 并行编译任务数。 |

显式使用推荐的 `r128*b512` 配置：

```bash
VANITY_MAX_REGISTER_COUNT=128 \
VANITY_THREADS_PER_BLOCK=512 \
bash ./run build
```

也可以与较低寄存器配置进行对比：

```bash
VANITY_MAX_REGISTER_COUNT=auto \
VANITY_THREADS_PER_BLOCK=256 \
bash ./run build
```

## 启动搜索

使用 `src/config.h` 中配置的默认前缀启动搜索：

```bash
bash ./run start
```

搜索一个或多个自定义 Base58 前缀：

```bash
bash ./run start --prefix SoL --prefix CUDA
```

可用的命令行参数：

| 参数 | 默认值 | 说明 |
| --- | --- | --- |
| `--prefix PREFIX` | `src/config.h` | 搜索固定的 Base58 前缀。可以重复指定，最多支持 16 个前缀。 |
| `--stop-after COUNT` | `1` | 找到指定数量的匹配密钥后停止。 |
| `--max-iterations COUNT` | `100000` | 达到指定搜索迭代次数后停止。 |
| `--help`、`-h` | 不适用 | 输出命令行帮助。 |

每个前缀必须非空，只能包含有效的 Base58 字符，长度不能超过 44 个字符。如果至少指定一个 `--prefix`，本次运行会使用命令行前缀替代 `src/config.h` 中的默认前缀列表。

示例：

```bash
# 找到一个匹配密钥后退出。
bash ./run start --prefix SoL --stop-after 1

# 搜索两个前缀，最多执行 500 次迭代。
bash ./run start --prefix GPU --prefix CUDA --max-iterations 500
```

按下 `Ctrl+C` 可以安全停止搜索并释放 GPU 资源。

## 结果文件

每次运行都会在当前工作目录下的 `result/` 目录中创建带时间戳的 CSV 文件：

```text
result/YYYY-MM-DDTHH-MM-SS.mmmZ.csv
```

搜索开始时会通过 `RESULT_FILE,path=...` 输出结果文件路径。设置 `VANITY_RESULT_DIR` 可以将结果写入其他目录：

```bash
VANITY_RESULT_DIR=/secure/path/vanity-results bash ./run start --prefix SoL
```

CSV 文件包含：

```text
time,pubkey,privkey
```

`pubkey` 和 `privkey` 字段均使用 Base58 编码。私钥高度敏感：程序会以严格权限创建结果文件，但仍需自行保护结果目录，并安全备份密钥。

## Fixed16 表工具

可以单独生成并验证固定位置的 16 位预计算表：

```bash
bash ./run precompute16
bash ./run verify-precompute16
```

默认生成路径为 `precomputed/fixed16-v1.bin`。可以通过 `VANITY_FIXED16_TABLE_PATH` 覆盖：

```bash
VANITY_FIXED16_TABLE_PATH=/path/to/fixed16-v1.bin bash ./run precompute16
```

## 参考项目
https://github.com/WincerChan/SolVanityCL

https://github.com/ChorusOne/solanity

https://github.com/vikulin/ed25519-gpu-vanity
<!-- 在这里补充相关项目、论文、基准测试和上游引用。 -->
