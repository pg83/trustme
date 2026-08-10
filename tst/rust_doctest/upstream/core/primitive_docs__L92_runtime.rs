// Extracted from library/core/src/primitive_docs.rs:92
#![allow(unused)]
fn main() {
    fn get_a_number() -> Option<u32> { None }
    loop {
    let num: u32 = match get_a_number() {
        Some(num) => num,
        None => break,
    };
    }
}
