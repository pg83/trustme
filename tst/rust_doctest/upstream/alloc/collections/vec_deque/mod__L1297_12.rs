// Extracted from library/alloc/src/collections/vec_deque/mod.rs:1297
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::VecDeque;

    let mut buf = VecDeque::new();
    buf.push_back(5);
    buf.push_back(3);
    buf.push_back(4);
    for num in buf.iter_mut() {
        *num = *num - 2;
    }
    let b: &[_] = &[&mut 3, &mut 1, &mut 2];
    assert_eq!(&buf.iter_mut().collect::<Vec<&mut i32>>()[..], b);
}
