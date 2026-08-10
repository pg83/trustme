// Extracted from library/core/src/primitive_docs.rs:1774
#![allow(unused)]
fn main() {
    let fnptr: fn(i32) -> i32 = |x| x+2;
    let fnptr_addr = fnptr as usize;
}
