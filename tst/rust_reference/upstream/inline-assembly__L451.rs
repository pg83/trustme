// Extracted from src/inline-assembly.md:451
#![allow(unused)]
fn main() {
    #[cfg(target_arch = "x86_64")]
    unsafe {
        core::arch::asm!("jmp {}", label {
            println!("Hello from inline assembly label");
        });
    }
}
