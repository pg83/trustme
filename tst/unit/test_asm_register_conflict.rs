//@ compile-fail: conflicts with register
// Explicit `asm!` registers must not name the same physical register twice,
// including through an alias or a sub-register: `ax` and `al` are both `rax`.
//
// Same shape as the Rust Reference example inline-assembly.md:538.
fn main() {
    #[cfg(target_arch = "x86_64")]
    unsafe {
        core::arch::asm!("", in("ax") 5, in("al") 4i8);
    }
    #[cfg(not(target_arch = "x86_64"))]
    core::compile_error!("Test not supported on this arch");
}
