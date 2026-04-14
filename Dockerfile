FROM ubuntu:22.04
RUN apt-get update && apt-get install -y \
    nasm binutils gcc-multilib grub-pc-bin xorriso mtools make
WORKDIR /osdev