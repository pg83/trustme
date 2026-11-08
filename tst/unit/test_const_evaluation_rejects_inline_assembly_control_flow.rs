//@ compile-fail: inline assembly is not allowed in constant evaluation
#![feature(asm_goto)]

const VALUE: u16 = {
    unsafe {
        core::arch::asm!("jmp {exit}", exit = label {}, options(nostack));
    }
    29
};

fn main() {
    let _ = VALUE;
}
