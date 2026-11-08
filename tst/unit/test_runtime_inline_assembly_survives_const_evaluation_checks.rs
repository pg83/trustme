#![feature(llvm_asm)]

fn main() {
    let mut value = 4u32;
    unsafe {
        core::arch::asm!("add {value:e}, 3", value = inout(reg) value, options(nomem, nostack));
    }
    let copied: u32;
    unsafe {
        llvm_asm!("movl $1, $0" : "=r"(copied) : "r"(value + 5) : "cc" : "volatile");
    }
    assert_eq!(copied, 12);
}
