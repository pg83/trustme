// Extracted from src/exotic-sizes.md:36
#![allow(unused)]
fn main() {
    // Can't be stored on the stack directly
    struct MySuperSlice {
        info: u32,
        data: [u8],
    }
}
