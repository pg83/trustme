// Extracted from library/core/src/slice/mod.rs:3811
#![allow(unused)]
fn main() {
    let src = [1, 2, 3, 4];
    let mut dst = [0, 0];

    // Because the slices have to be the same length,
    // we slice the source slice from four elements
    // to two. It will panic if we don't do this.
    dst.copy_from_slice(&src[2..]);

    assert_eq!(src, [1, 2, 3, 4]);
    assert_eq!(dst, [3, 4]);
}
