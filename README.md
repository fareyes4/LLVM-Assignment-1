LLVM Assignment 1

This project is a simple Loop Invariant Code Motion (LICM) pass built on top of llvm-tutor

Build
cmake -B build -S . -DLT_LLVM_INSTALL_DIR="$(llvm-config --prefix)"
cmake --build build --target SimpleLICM -j

Test
clang -O0 -Xclang -disable-O0-optnone -S -emit-llvm inputs/licm_simple.c -o inputs/licm_simple.ll

cd build
opt -load-pass-plugin ./lib/libSimpleLICM.so \
    -passes='function(mem2reg,loop-simplify,loop(simple-licm))' \
    -S ../inputs/licm_simple.ll -o ../outputs/licm_simple_licm.ll \
    2> ../outputs/licm_log.txt

