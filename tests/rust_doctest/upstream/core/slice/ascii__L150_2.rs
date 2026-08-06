// Extracted from library/core/src/slice/ascii.rs:150
#![allow(unused)]
fn main() {
    assert_eq!(b" \t hello world\n".trim_ascii_start(), b"hello world\n");
    assert_eq!(b"  ".trim_ascii_start(), b"");
    assert_eq!(b"".trim_ascii_start(), b"");
}
