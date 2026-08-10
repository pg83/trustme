// Extracted from library/std/src/io/mod.rs:1600
#![allow(unused)]
#![feature(io_slice_as_bytes)]
fn main() {
    use std::io::IoSlice;

    let data = b"abcdef";

    let mut io_slice = IoSlice::new(data);
    let tail = &io_slice.as_slice()[3..];

    // This works because `tail` doesn't borrow `io_slice`
    io_slice = IoSlice::new(tail);

    assert_eq!(io_slice.as_slice(), b"def");
}
