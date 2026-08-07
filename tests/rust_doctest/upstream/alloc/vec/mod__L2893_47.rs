// Extracted from library/alloc/src/vec/mod.rs:2893
#![allow(unused)]
extern crate alloc;
fn main() {
    let mut vec = vec!['a', 'b', 'c'];
    let vec2 = vec.split_off(1);
    assert_eq!(vec, ['a']);
    assert_eq!(vec2, ['b', 'c']);
}
