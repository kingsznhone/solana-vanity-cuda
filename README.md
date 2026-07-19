# Solana Vanity CUDA

[中文文档](README.zh-CN.md) | **English**

GPU-accelerated Solana vanity address search with an aggressively optimized CUDA
Ed25519 pipeline. This project is designed to brute-force Solana public keys
until their Base58 representation matches one or more fixed prefixes.

The implementation pushes the search path hard: it uses skip-zero techniques,
a fixed-position 16-bit precomputation table, GPU-side prefix range matching,
and low-level CUDA tuning controls for register usage, block size, and work per
kernel launch. Instead of comparing strings character by character, the target
prefix is converted into a leading numeric range, allowing the GPU to perform a
compact range check during the search.

This optimization strategy is intentionally specialized. **Suffix searches are
not supported.** Only fixed leading Base58 prefixes are supported.

### Reported performance

The searcher has been tested on an NVIDIA RTX 3070 Ti and RTX 5090:

- RTX 3070 Ti: approximately **30 MH/s**
- RTX 5090: approximately **220 MH/s**

In these tests, the throughput was **200%+ of other GPU vanity searchers**.
Actual performance depends on the CUDA toolkit, driver, clock settings,
thermal limits, selected build parameters, and the number of visible GPUs.

Additional tuning tests across multiple GPU platforms found that the
`r128*b512` configuration consistently delivered the best overall performance:

- `r128`: `VANITY_MAX_REGISTER_COUNT=128`
- `b512`: `VANITY_THREADS_PER_BLOCK=512`

This is also the default build configuration. It is a practical baseline for
benchmarking, although individual cards may still benefit from local tuning.

## Environment Requirements

- Linux with a working NVIDIA driver
- NVIDIA GPU with CUDA support
- CUDA Toolkit, including `nvcc`
- CMake 3.22 or newer
- A C17/C++17-capable host compiler
- `pkg-config`
- OpenSSL development files (`libssl-dev` on Debian/Ubuntu)
- `nvidia-smi` available in `PATH`

The launcher automatically uses the native CUDA architecture with CMake 3.24+
and newer. With older CMake versions it queries the first visible GPU through
`nvidia-smi`. You can override this behavior with `CUDA_ARCHITECTURES`.

## Use a Release Binary

Prebuilt Linux and Windows binaries are available from the [0.1.0 Release page](https://github.com/kingsznhone/solana-vanity-cuda/releases/tag/0.1.0).
Download the binary for your platform and run it directly. The Linux binary
must be marked executable after downloading:

```bash
chmod +x solana-vanity-cuda-linux-x86_64
./solana-vanity-cuda-linux-x86_64 --help
./solana-vanity-cuda-linux-x86_64 --prefix SoL --stop-after 1
```

On Windows PowerShell, run the `.exe` directly:

```powershell
.\solana-vanity-cuda-windows-x86_64.exe --help
.\solana-vanity-cuda-windows-x86_64.exe --prefix SoL --stop-after 1
```

You can provide more than one `--prefix`, or use `--max-iterations COUNT` to
bound a search. Release binaries do not require `nvcc`, CMake, or OpenSSL
development packages, but runtime use still requires a compatible NVIDIA
driver and CUDA-capable GPU. Keep generated keypairs and result files secure.

## Build

The repository includes a build launcher that detects CUDA and keeps the build
configuration in one place:

```bash
bash ./run build
```

Before configuring CMake, the launcher checks the Linux distribution, CMake,
GCC/G++, Make, `nvcc`, `pkg-config`, OpenSSL development files, and the GPU
environment used for native architecture detection. Run the checks without
compiling with:

```bash
bash ./run doctor
```

On Ubuntu 22.04/24.04, the usual host dependencies are:

```bash
sudo apt update
sudo apt install --no-install-recommends build-essential cmake pkg-config libssl-dev
```

The NVIDIA Driver and CUDA Toolkit are installed separately. A build without a
visible GPU is supported when `CUDA_ARCHITECTURES` is set explicitly, for
example `CUDA_ARCHITECTURES=86 bash ./run build`.

The Linux and Windows release workflows pin CUDA Toolkit 12.9.1 and build one
universal binary per platform. `CUDA_ARCHITECTURES=universal` expands to the
standard CUDA 12.9 targets from `sm_50` through `sm_121`. CUDA 12.9.x is used
because the release must retain Maxwell support; CUDA 13.x is not a valid
toolkit for this universal profile.

The main executable is written to:

```text
build/release/solana-vanity-cuda
```

### Versioning

The application version is defined in `CMakeLists.txt` and defaults to
`0.1.0`. Check the version of a built executable with:

```bash
build/release/solana-vanity-cuda --version
```

To override the version for a build, pass `VANITY_VERSION` to CMake:

```bash
cmake -S . -B build-version \
	-DCMAKE_BUILD_TYPE=Release \
	-DVANITY_VERSION=0.2.0
```

Use Semantic Versioning for releases. Keep a release tag such as `v0.2.0`
consistent with the executable version `0.2.0`. The GitHub Actions workflows
remain manually triggered; release publishing is performed separately.

Windows release builds use static OpenSSL and MSVC runtime libraries, and CUDA
runtime libraries are linked statically where supported. The NVIDIA driver is
still required at runtime because `nvcuda.dll` is supplied by the installed
GPU driver and cannot be bundled into the executable.

To select a specific CUDA compiler, architecture, build directory, or build
parallelism:

```bash
CUDACXX=/usr/local/cuda/bin/nvcc \
CUDA_ARCHITECTURES=89 \
BUILD_DIR=build \
BUILD_JOBS=16 \
bash ./run build
```

## Important Build Parameters

These environment variables are passed through to CMake:

| Variable | Values | Default | Description |
| --- | --- | --- | --- |
| `CUDA_ARCHITECTURES` | CUDA architecture, `native`, or `universal` | `native` | Target GPU architecture, for example `89` for an RTX 3070 Ti, `120` for an RTX 5090, or `universal` for one binary containing all standard targets supported by the pinned CUDA Toolkit. |
| `VANITY_MAX_REGISTER_COUNT` | `auto` or `128` | `128` | CUDA register limit. `auto` enables launch bounds; `128` applies `--maxrregcount=128`. |
| `VANITY_THREADS_PER_BLOCK` | `256` or `512` | `512` | CUDA threads per block. |
| `VANITY_ATTEMPTS_PER_EXECUTION` | Positive integer | `10000` | Candidate keys processed by each thread per kernel launch. |
| `BUILD_TYPE` | CMake build type | `Release` | Build configuration used by the launcher. |
| `BUILD_DIR` | Directory path | `./build` | CMake build directory. |
| `BUILD_JOBS` | Positive integer | Detected CPU count | Number of parallel build jobs. |

For example, to try the recommended `r128*b512` configuration explicitly:

```bash
VANITY_MAX_REGISTER_COUNT=128 \
VANITY_THREADS_PER_BLOCK=512 \
bash ./run build
```

To compare it with a lower-register configuration:

```bash
VANITY_MAX_REGISTER_COUNT=auto \
VANITY_THREADS_PER_BLOCK=256 \
bash ./run build
```

The best settings are GPU-dependent. Benchmark a few configurations on the
target card instead of assuming that the defaults are optimal everywhere.

## Running a Search

Start the search with the default prefix configured in `src/config.h`:

```bash
bash ./run start
```

Search one or more custom Base58 prefixes:

```bash
bash ./run start --prefix SoL --prefix CUDA
```

Available command-line options:

| Option | Default | Description |
| --- | --- | --- |
| `--prefix PREFIX` | `src/config.h` | Search for a fixed leading Base58 prefix. May be specified multiple times, up to 16 prefixes. |
| `--stop-after COUNT` | `1` | Stop after the requested number of matching keys have been found. |
| `--max-iterations COUNT` | `100000` | Stop after the requested number of search iterations. |
| `--version`, `-V` | N/A | Print the executable version. |
| `--help`, `-h` | N/A | Print command-line usage. |

Each prefix must be non-empty, contain only valid Base58 characters, and be no
longer than 44 characters. Supplying at least one `--prefix` replaces the
prefix list from `src/config.h` for that run.

Examples:

```bash
# Find one matching key, then exit.
bash ./run start --prefix SoL --stop-after 1

# Search two prefixes for at most 500 iterations.
bash ./run start --prefix GPU --prefix CUDA --max-iterations 500
```

Press `Ctrl+C` to stop the search cleanly and release GPU resources.

## Results

Every run creates a timestamped CSV file in the `result/` directory relative
to the current working directory:

```text
result/YYYY-MM-DDTHH-MM-SS.mmmZ.csv
```

The output path is printed as `RESULT_FILE,path=...` when the search starts.
Set `VANITY_RESULT_DIR` to write results somewhere else:

```bash
VANITY_RESULT_DIR=/secure/path/vanity-results bash ./run start --prefix SoL
```

The CSV contains:

```text
time,pubkey,privkey
```

The `pubkey` and `privkey` fields are Base58-encoded. Treat the private key as
highly sensitive: result files are created with restrictive permissions, but
you are responsible for protecting the directory and backing up keys securely.

## Fixed16 Table Utilities

The fixed-position 16-bit table can be generated and verified independently:

```bash
bash ./run precompute16
bash ./run verify-precompute16
```

The default artifact path is `precomputed/fixed16-v1.bin`. Override it with
`VANITY_FIXED16_TABLE_PATH`:

```bash
VANITY_FIXED16_TABLE_PATH=/path/to/fixed16-v1.bin bash ./run precompute16
```

## References
https://github.com/WincerChan/SolVanityCL

https://github.com/ChorusOne/solanity

https://github.com/vikulin/ed25519-gpu-vanity
