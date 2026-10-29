//@ crate-type: lib
//@ compile-flags: -Ccodegen-type=monomir -Cemit-cpp-only -Zmir-opt-level=0
//@ output-contains: .mir ASM2_GOTO (
//@ output-contains: .mir ASM (
#![feature(asm_goto, llvm_asm)]
pub fn conditional(value: u32) -> u8 {
    unsafe {
        std::arch::asm!("test {value:e}, {mask}", "jnz {hit}", value = in(reg) value, mask = const 1,
            hit = label { return 7; }, options(nostack));
    }
    3
}
pub fn direct() {
    unsafe { std::arch::asm!("jmp {hit}", hit = label { return; }, options(noreturn)); }
}
pub unsafe fn legacy(value: u32) -> u32 {
    let output: u32;
    llvm_asm!("movl $1, $0" : "=r"(output) : "r"(value) : "cc" : "volatile");
    output
}
