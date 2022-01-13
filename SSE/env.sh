PROJECTHOME=$(pwd)
sysOS=`uname -s`
LLVMHome="llvm-12.0.0.obj"
Z3DIR="z3.obj"
install_path=`npm root`
export Z3_DIR=$PROJECTHOME/$Z3DIR
export LLVM_DIR=$install_path/$LLVMHome
export PATH=$Z3_DIR/bin:$PATH
export PATH=$LLVM_DIR/bin:$PATH
export PATH=$PROJECTHOME/bin:$PATH
echo "export LLVM_DIR=$install_path/$LLVMHome" >> ~/.bashrc
echo "export PATH=$LLVM_DIR/bin:$PROJECTHOME/bin:$PATH" >> ~/.bashrc
if [[ $sysOS == "Darwin" ]]
then 
    export SVF_DIR=$install_path/SVF
elif [[ $sysOS == "Linux" ]]
then 
    export SVF_DIR=$install_path/SVF
fi 

echo "LLVM_DIR="$LLVM_DIR
echo "SVF_DIR="$SVF_DIR
echo "Z3_DIR="$Z3_DIR