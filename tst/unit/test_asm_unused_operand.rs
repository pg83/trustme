//@ compile-fail: unused asm argument
// An `asm!` operand that names no explicit register has to be referenced by the
// template, otherwise nothing can reach it.
//
// Same shape as the Rust Reference example inline-assembly.md:247.
fn main() {
    #[cfg(target_arch = "x86_64")]
    unsafe {
        core::arch::asm!("", in(reg) 5);
    }
    #[cfg(not(target_arch = "x86_64"))]
    core::compile_error!("Test not supported on this arch");
}
