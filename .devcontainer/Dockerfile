# Debian 11 (Bullseye), as per the VM provided by COM S 352 teaching staff.
FROM debian:bullseye

# Update OS
RUN apt-get update && \
    apt-get upgrade -y

# Install dependencies
RUN apt-get install -y git build-essential gdb-multiarch qemu-system-misc gcc-riscv64-linux-gnu binutils-riscv64-linux-gnu 

# Set user
USER root