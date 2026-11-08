//@ compile-fail: inline assembly is not allowed in constant evaluation
#![feature(llvm_asm)]

const VALUE: u16 = {
    unsafe { llvm_asm!("" : : : : "volatile"); }
    17
};

fn main() {
    let _ = VALUE;
}
