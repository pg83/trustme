// Extracted from library/std/src/macros.rs:231
#![allow(unused)]
fn main() {
    let a = 2;
    let b = dbg!(a * 2) + 1;
    //      ^-- prints: [src/main.rs:2:9] a * 2 = 4
    assert_eq!(b, 5);
}
