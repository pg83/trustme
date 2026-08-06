// Extracted from library/core/src/ffi/c_str.rs:508
#![allow(unused)]
fn main() {
    assert_eq!(c"foo".count_bytes(), 3);
    assert_eq!(c"".count_bytes(), 0);
}
