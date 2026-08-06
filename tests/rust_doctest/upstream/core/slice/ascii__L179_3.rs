// Extracted from library/core/src/slice/ascii.rs:179
#![allow(unused)]
fn main() {
    assert_eq!(b"\r hello world\n ".trim_ascii_end(), b"\r hello world");
    assert_eq!(b"  ".trim_ascii_end(), b"");
    assert_eq!(b"".trim_ascii_end(), b"");
}
