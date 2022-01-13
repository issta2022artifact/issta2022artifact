#!/bin/bash

7z x FFMpeg+Qemu.7z

mkdir projects
mkdir outputs

git clone https://github.com/FFmpeg/FFmpeg.git projects/FFmpeg
git clone https://github.com/qemu/qemu.git projects/qemu
