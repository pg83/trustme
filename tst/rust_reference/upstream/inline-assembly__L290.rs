// Extracted from src/inline-assembly.md:290
#![allow(unused)]
fn main() {
    // This is rejected because `a = out(reg) x` does not parse as a
    // template string.
    core::arch::asm!(
        #[cfg(false)]
        a = out(reg) x, // ERROR.
        "",
    );
}
