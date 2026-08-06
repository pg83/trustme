// Extracted from src/inline-assembly.md:487
fn main() {}
fn foo() {}

#[cfg(target_arch = "x86_64")]
// `const` and `sym` are both allowed, however
core::arch::global_asm!("/* {} {} */", const 0, sym foo);
