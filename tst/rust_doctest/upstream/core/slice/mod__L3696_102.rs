// Extracted from library/core/src/slice/mod.rs:3696
#![allow(unused)]
fn main() {
    let mut buf = vec![0; 10];
    buf.fill(1);
    assert_eq!(buf, vec![1; 10]);
}
