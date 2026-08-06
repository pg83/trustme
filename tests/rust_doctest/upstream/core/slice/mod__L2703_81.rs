// Extracted from library/core/src/slice/mod.rs:2703
#![allow(unused)]
fn main() {
    let v = &[10, 40, 30];
    assert_eq!(v.strip_suffix(&[30]), Some(&[10, 40][..]));
    assert_eq!(v.strip_suffix(&[40, 30]), Some(&[10][..]));
    assert_eq!(v.strip_suffix(&[10, 40, 30]), Some(&[][..]));
    assert_eq!(v.strip_suffix(&[50]), None);
    assert_eq!(v.strip_suffix(&[50, 30]), None);
}
