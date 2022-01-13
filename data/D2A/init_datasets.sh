#!/bin/bash
#some essential packages
sudo apt-get install p7zip-full yasm libstdc++6

# download original dataset from d2a's ibm cloud instance
mkdir datasets
wget https://dax-cdn.cdn.appdomain.cloud/dax-d2a/1.1.0/ffmpeg.tar.gz?_ga=2.35725280.1915232510.1628568061-1866359943.1627631789 -O datasets/ffmpeg.gz
wget https://dax-cdn.cdn.appdomain.cloud/dax-d2a/1.1.0/httpd.tar.gz?_ga=2.35725280.1915232510.1628568061-1866359943.1627631789 -O datasets/httpd.gz
wget https://dax-cdn.cdn.appdomain.cloud/dax-d2a/1.1.0/libav.tar.gz?_ga=2.142168818.1915232510.1628568061-1866359943.1627631789 -O datasets/libav.gz
wget https://dax-cdn.cdn.appdomain.cloud/dax-d2a/1.1.0/libtiff.tar.gz?_ga=2.138115056.1915232510.1628568061-1866359943.1627631789 -O datasets/libtiff.gz
wget https://dax-cdn.cdn.appdomain.cloud/dax-d2a/1.1.0/nginx.tar.gz?_ga=2.36329312.1915232510.1628568061-1866359943.1627631789 -O datasets/nginx.gz
wget https://dax-cdn.cdn.appdomain.cloud/dax-d2a/1.1.0/openssl.tar.gz?_ga=2.138115056.1915232510.1628568061-1866359943.1627631789 -O datasets/openssl.gz

# decompose all datasets
cd datasets

mkdir nginx
mkdir httpd
mkdir libtiff
mkdir openssl
mkdir ffmpeg
mkdir libav

tar -xf nginx.gz -C nginx --strip-components 1
tar -xf httpd.gz -C httpd --strip-components 1
tar -xf libtiff.gz -C libtiff --strip-components 1
tar -xf openssl.gz -C openssl --strip-components 1
tar -xf ffmpeg.gz -C ffmpeg --strip-components 1
tar -xf libav.gz -C libav --strip-components 1
cd ../

#download following projects from GitHub
mkdir projects
git clone https://github.com/qemu/qemu projects/qemu
git clone https://github.com/FFmpeg/FFmpeg projects/ffmpeg
git clone https://github.com/nginx/nginx projects/nginx
git clone https://github.com/openssl/openssl.git projects/openssl
git clone https://gitlab.com/libtiff/libtiff.git projects/libtiff
git clone https://github.com/libav/libav.git projects/libav
git clone https://github.com/apache/httpd.git projects/httpd

mkdir outputs
