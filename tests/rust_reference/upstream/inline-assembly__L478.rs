// Extracted from src/inline-assembly.md:478
fn main() {}
// register operands aren't allowed, since we aren't in a function
#[cfg(target_arch = "x86_64")]
core::arch::global_asm!("", in(reg) 5);
// ERROR: the `in` operand cannot be used with `global_asm!`
#[cfg(not(target_arch = "x86_64"))] core::compile_error!("Test not supported on this arch");
