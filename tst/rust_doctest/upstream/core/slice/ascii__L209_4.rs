// Extracted from library/core/src/slice/ascii.rs:209
#![allow(unused)]
fn main() {
    assert_eq!(b"\r hello world\n ".trim_ascii(), b"hello world");
    assert_eq!(b"  ".trim_ascii(), b"");
    assert_eq!(b"".trim_ascii(), b"");
}
