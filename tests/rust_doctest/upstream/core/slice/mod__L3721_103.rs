// Extracted from library/core/src/slice/mod.rs:3721
#![allow(unused)]
fn main() {
    let mut buf = vec![1; 10];
    buf.fill_with(Default::default);
    assert_eq!(buf, vec![0; 10]);
}
