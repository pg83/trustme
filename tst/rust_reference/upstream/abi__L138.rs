// Extracted from src/abi.md:138
#![allow(unused)]
fn main() {
    #[cfg(target_os = "linux")] {
    #[unsafe(no_mangle)]
    #[unsafe(link_section = ".example_section")]
    pub static VAR1: u32 = 1;
    }
}
