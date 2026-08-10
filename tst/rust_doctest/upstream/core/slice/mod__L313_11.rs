// Extracted from library/core/src/slice/mod.rs:313
#![allow(unused)]
fn main() {
    let u = [10, 40, 30];
    assert_eq!(Some(&[10, 40]), u.first_chunk::<2>());

    let v: &[i32] = &[10];
    assert_eq!(None, v.first_chunk::<2>());

    let w: &[i32] = &[];
    assert_eq!(Some(&[]), w.first_chunk::<0>());
}
