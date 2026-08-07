// Extracted from library/core/src/slice/mod.rs:2737
#![allow(unused)]
#![feature(trim_prefix_suffix)]
fn main() {

    let v = &[10, 40, 30];

    // Prefix present - removes it
    assert_eq!(v.trim_prefix(&[10]), &[40, 30][..]);
    assert_eq!(v.trim_prefix(&[10, 40]), &[30][..]);
    assert_eq!(v.trim_prefix(&[10, 40, 30]), &[][..]);

    // Prefix absent - returns original slice
    assert_eq!(v.trim_prefix(&[50]), &[10, 40, 30][..]);
    assert_eq!(v.trim_prefix(&[10, 50]), &[10, 40, 30][..]);

    let prefix : &str = "he";
    assert_eq!(b"hello".trim_prefix(prefix.as_bytes()), b"llo".as_ref());
}
