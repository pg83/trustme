// Extracted from library/std/src/collections/mod.rs:242
#![allow(unused)]
fn main() {
    use std::collections::VecDeque;
    
    let vec = [1, 2, 3, 4];
    let buf: VecDeque<_> = vec.into_iter().collect();
}
