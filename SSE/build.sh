#!/bin/bash
SVFHOME=$(pwd)
sysOS=`uname -s`
MacZ3="https://github.com/Z3Prover/z3/releases/download/z3-4.8.10/z3-4.8.10-x64-osx-10.15.7.zip"
UbuntuZ3="https://github.com/Z3Prover/z3/releases/download/z3-4.8.10/z3-4.8.10-x64-ubuntu-18.04.zip"
Z3Home="z3.obj"

# Download Z3 if need be.
if [ ! -d "$Z3_DIR" ]
then
    if [ ! -d "$Z3Home" ]
    then
    if [[ "$sysOS" == "Darwin" ]]
    then
        curl -L $MacZ3 -o "z3.zip"
    elif [[ "$sysOS" == "Linux" ]]
    then
        wget -c "$UbuntuZ3" -O "z3.zip"
    else
        echo "Cannot download file for non-Darwin/-Linux."
    fi
    unzip -q "z3.zip" && mv ./z3-* ./$Z3Home
    rm z3.zip
    fi

    export Z3_DIR="$SVFHOME/$Z3Home"
fi
echo $Z3_DIR

# npm install LLVM and SVF binary
npm i --silent svf-lib --prefix ${HOME}

# setup for env 
source ./env.sh

# generate bc file for ctest
bash Test-Suite/generate_bc.sh

if [[ $1 == 'Debug' ]]
then
    rm -rf ./'Debug-build'
    mkdir ./'Debug-build'
    cd ./'Debug-build'
	cmake -D CMAKE_BUILD_TYPE:STRING=Debug ../
else
    rm -rf ./'Release-build'
    mkdir ./'Release-build'
    cd ./'Release-build'
    cmake ../
fi
make -j 4

