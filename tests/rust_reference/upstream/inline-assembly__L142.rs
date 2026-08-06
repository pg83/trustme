// Extracted from src/inline-assembly.md:142
fn main() {}
#[cfg(target_arch = "x86_64")]
core::arch::global_asm!("/* {} */", const 0);
