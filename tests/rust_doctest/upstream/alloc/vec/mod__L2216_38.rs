// Extracted from library/alloc/src/vec/mod.rs:2216
#![allow(unused)]
extern crate alloc;
fn main() {
    let mut vec = vec![1, 2, 3, 4];
    vec.retain_mut(|x| if *x <= 3 {
        *x += 1;
        true
    } else {
        false
    });
    assert_eq!(vec, [2, 3, 4]);
}
