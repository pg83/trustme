// Extracted from src/phantom-data.md:9
#![allow(unused)]
fn main() {
    struct Iter<'a, T: 'a> {
        ptr: *const T,
        end: *const T,
    }
}
