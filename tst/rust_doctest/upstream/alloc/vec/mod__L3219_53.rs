// Extracted from library/alloc/src/vec/mod.rs:3219
#![allow(unused)]
extern crate alloc;
fn main() {
    let mut vec = vec!["hello"];
    vec.resize(3, "world");
    assert_eq!(vec, ["hello", "world", "world"]);

    let mut vec = vec!['a', 'b', 'c', 'd'];
    vec.resize(2, '_');
    assert_eq!(vec, ['a', 'b']);
}
