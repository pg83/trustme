// Extracted from library/std/src/io/mod.rs:1436
#![allow(unused)]
#![feature(io_slice_as_bytes)]
fn main() {
    use std::io::IoSliceMut;
    
    let mut data = *b"abcdef";
    let io_slice = IoSliceMut::new(&mut data);
    io_slice.into_slice()[0] = b'A';
    
    assert_eq!(&data, b"Abcdef");
}
