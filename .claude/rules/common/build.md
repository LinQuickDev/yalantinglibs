# Build Mooncake

## End-to-end build
end-to-end build the Mooncake using following commands:

```
export CXXFLAGS="-w"
export CFLAGS="-w"
export PATH="/usr/local/go/bin:$PATH"
export GOPROXY="https://goproxy.cn,direct"
export GOSUMDB="off"

cmake -B build \
  -DBUILD_SHARED_LIBS=ON \
  -DUSE_UB:BOOL=OFF \
  -DWITH_TE=ON \
  -DWITH_P2P_STORE=ON \
  -DWITH_STORE=ON \
  -DUSE_ETCD=ON \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_UNIT_TESTS:BOOL=off

cmake --build build -j8
```

Note:
- `GOPROXY` and `GOSUMDB` are required for Go module downloads in p2p-store
- `CXXFLAGS`/`CFLAGS` use `-w` to suppress warnings (note: not recursive `CXXFLAGS -w`)

## URMA Build Note

When building with URMA enabled (`-DYLT_ENABLE_URMA=ON`), the linker may fail with:
```
/usr/bin/ld: cannot find -lurma: No such file or directory
```

This is expected if URMA library is not installed on the system. The code is correct; this is an environment configuration issue. To suppress this error during development, either:
1. Install the URMA library
2. Use a build system that links URMA conditionally (not yet implemented)