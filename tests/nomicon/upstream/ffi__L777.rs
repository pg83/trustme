// Extracted from src/ffi.md:777
#![allow(unused)]
fn main() {
    #[unsafe(no_mangle)]
    unsafe extern "C-unwind" fn example() {
        panic!("Uh oh");
    }
}
