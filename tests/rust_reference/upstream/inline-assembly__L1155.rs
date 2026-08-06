// Extracted from src/inline-assembly.md:1155
#![allow(unused)]
fn main() {
    #[cfg(target_arch = "x86_64")] {
    let mut x = 0;
    // We cannot modify outside memory when `readonly` is specified
    unsafe {
        core::arch::asm!("mov dword ptr[{}], 1", in(reg) &mut x, options(readonly))
    }
    }
}
