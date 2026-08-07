// Extracted from library/core/src/slice/mod.rs:143
#![allow(unused)]
fn main() {
    let v = [10, 40, 30];
    assert_eq!(Some(&10), v.first());

    let w: &[i32] = &[];
    assert_eq!(None, w.first());
}
