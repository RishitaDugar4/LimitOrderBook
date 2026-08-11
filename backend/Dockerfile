# Multi-stage build for the FastAPI backend + compiled C++ matching engine.
#
# Stage 1 compiles the pybind11 module against Python 3.11. Stage 2 is a slim
# 3.11 runtime that imports that module -- the Python versions MUST match
# (3.11 <-> 3.11) or the extension's ABI tag won't load. Build context is the
# REPO ROOT (see docker-compose.yml) so the C++ sources are available.

# ---- Stage 1: build the C++ / pybind11 module ------------------------
FROM python:3.11-bookworm AS cpp-build

RUN apt-get update && apt-get install -y --no-install-recommends \
        cmake g++ git make ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src

# Copy the C++ sources, CMake, and bindings. (backend/frontend excluded via
# .dockerignore so they don't bust this layer's cache.)
COPY CMakeLists.txt ./
COPY *.cpp ./
COPY include ./include
COPY bindings ./bindings

RUN cmake -S . -B build \
        -DCMAKE_BUILD_TYPE=Release \
        -DLOB_BUILD_PYTHON=ON \
        -DLOB_BUILD_TESTS=OFF \
        -DLOB_BUILD_BENCHMARKS=OFF \
    && cmake --build build --target orderbook_py -j "$(nproc)"

# Normalize the built module to a predictable name/location.
RUN mkdir -p /out && cp build/orderbook_py*.so /out/

# ---- Stage 2: runtime ------------------------------------------------
FROM python:3.11-slim-bookworm AS runtime

# libstdc++ / libgomp for the compiled extension at runtime.
RUN apt-get update && apt-get install -y --no-install-recommends \
        libstdc++6 libgomp1 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY backend/requirements.txt ./requirements.txt
RUN pip install --no-cache-dir -r requirements.txt

COPY backend ./backend
COPY --from=cpp-build /out/ /app/native/

ENV LOB_MODULE_PATH=/app/native
ENV PYTHONUNBUFFERED=1

EXPOSE 8000
CMD ["uvicorn", "backend.main:app", "--host", "0.0.0.0", "--port", "8000"]