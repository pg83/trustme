// Extracted from src/inline-assembly.md:844
#![allow(unused)]
fn main() {
    #[cfg(target_arch = "x86_64")] {
    let z = 0i64;
    // rax is an alias for eax and ax
    unsafe { core::arch::asm!("", in("rax") z); }
    }
}
