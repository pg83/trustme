// Extracted from src/destructors.md:98
#![allow(unused)]
fn main() {
    struct Boxy<T> {
        data1: Box<T>,
        data2: Box<T>,
        info: u32,
    }
}
