// Extracted from library/core/src/primitive_docs.rs:869
#![allow(unused)]
fn main() {
    let x = [1, 2, 3];
    let empty = &x[0..0];   // subslice before the first element
    assert_eq!(empty, &[]);
    let empty = &x[..0];    // same as &x[0..0]
    assert_eq!(empty, &[]);
    let empty = &x[1..1];   // empty subslice in the middle
    assert_eq!(empty, &[]);
    let empty = &x[3..3];   // subslice after the last element
    assert_eq!(empty, &[]);
    let empty = &x[3..];    // same as &x[3..3]
    assert_eq!(empty, &[]);
}
