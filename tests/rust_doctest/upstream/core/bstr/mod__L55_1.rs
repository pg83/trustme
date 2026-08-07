// Extracted from library/core/src/bstr/mod.rs:55
#![allow(unused)]
#![feature(bstr)]
fn main() {
    use std::bstr::ByteStr;
    let a = ByteStr::new(b"abc");
    let b = ByteStr::new(&b"abc"[..]);
    let c = ByteStr::new("abc");

    assert_eq!(a, b);
    assert_eq!(a, c);
}
