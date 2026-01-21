# Project README

Minimal full-text search engine with a C++ HTTP server, C++ CLI client, and a small web UI.

## Requirements
- CMake 3.16+
- C++17 compiler
- (Linux) pthreads
- (Windows) Winsock (auto via MSVC/MinGW)

## Build
From the repository root:

```sh
cmake -S . -B build
cmake --build build --config Release
```

Built binaries are placed under `build/` (platform-specific subfolder on Windows).

## Run the server
Set a dataset path in `config.env` (or pass `--dataset` on the command line).

Example:

```sh
./build/server --config config.env
```

Useful flags (override `config.env`):
- `--host 0.0.0.0`
- `--port 8080`
- `--dataset /path/to/dataset`
- `--threads 4`
- `--web_root web`

Then open the web UI:
- `http://127.0.0.1:8080/`

## Build the index (from web UI or CLI)
From the web UI, click "Build".

From CLI:

```sh
./build/client_cli --host 127.0.0.1 --port 8080 build --dataset "/path/to/dataset" --threads 4 --incremental true
```

## Search
From CLI:

```sh
./build/client_cli --host 127.0.0.1 --port 8080 search --q "hello world" --topk 20
```

From web UI:
- Enter query and press "Search".

## Dataset selector (optional)
Create a smaller dataset from a large corpus:

```sh
./build/dataset_selector --input "/path/to/large_corpus" --output "./data/subset1" --count 5000 --random --seed 42
```

## Load testing (optional)
Search load test against the running server:

```sh
./build/load_test --mode search --host 127.0.0.1 --port 8080 --clients 50 --duration_s 10 --q "hello" --topk 20 --csv results.csv
```

Local build benchmark (no server needed):

```sh
./build/load_test --mode build --dataset "/path/to/dataset" --threads_list "1,2,4,8" --csv build.csv
```

## Notes
- The server indexes `.txt` files by default.
- Web assets are copied next to the server binary after build.
