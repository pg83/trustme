// Extracted from library/core/src/option.rs:942
#![allow(unused)]
fn main() {
    let slice: &[u8] = &[];
    let item = slice.get(0)
        .expect("slice should not be empty");
}
