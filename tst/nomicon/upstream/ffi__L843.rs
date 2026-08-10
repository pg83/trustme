// Extracted from src/ffi.md:843
#![allow(unused)]
fn main() {
    #[unsafe(no_mangle)]
    extern "C" fn assert_nonzero(input: u32) {
        assert!(input != 0)
    }
}
