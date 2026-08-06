// Extracted from src/inline-assembly.md:1312
fn main() {}
#[cfg(target_arch = "x86_64")]
// nomem is useless on global_asm!
core::arch::global_asm!("", options(nomem));
#[cfg(not(target_arch = "x86_64"))] core::compile_error!("Test not supported on this arch");
