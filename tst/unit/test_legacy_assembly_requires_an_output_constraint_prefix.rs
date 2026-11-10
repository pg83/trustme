#![feature(llvm_asm)]
//@ compile-fail: Assembly output specifiers must start with =

fn main() {
    let mut value = 0u32;
    unsafe {
        llvm_asm!("" : "r"(value) : : : "volatile");
    }
}
