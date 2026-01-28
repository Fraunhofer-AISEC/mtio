FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive
SHELL ["/bin/bash", "-eo", "pipefail", "-c"]

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
      ca-certificates \
      # C/C++ runtime
      libc6 libgcc-s1 libstdc++6 \
      # Python 3.10 runtime + libpython SONAME
      python3.10-minimal libpython3.10 \
      # compression & parsing
      zlib1g liblzma5 libzstd1 libexpat1 \
      # tcmalloc & unwind
      libgoogle-perftools4 libunwind8 \
      # protobuf
      libprotobuf23 \
      # HDF5 (serial ABI 103) + sz/aec
      libhdf5-103-1 libhdf5-cpp-103-1 libsz2 libaec0 \
      # PNG
      libpng16-16 \
      # TLS/crypto
      libssl3 \
      # curl stack and deps
      libcurl4 libnghttp2-14 libidn2-0 librtmp1 libssh-4 libpsl5 \
      # GnuTLS stack (used by some curl builds)
      libgnutls30 libhogweed6 libnettle8 libgmp10 \
      # Kerberos / LDAP (curl optional features)
      libgssapi-krb5-2 libkrb5-3 libk5crypto3 libcom-err2 libkrb5support0 \
      libldap-2.5-0 \
      # brotli decoder (curl may dlopen it)
      libbrotli1 \
      openssl \
    && rm -rf /var/lib/apt/lists/*

# Put gem5 in a fixed path
WORKDIR /opt/gem5
COPY gem5 /opt/gem5/

WORKDIR /opt/gem5


