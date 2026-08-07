// Extracted from library/std/src/io/mod.rs:1358
#![allow(unused)]
fn main() {
    use std::io::IoSliceMut;
    use std::ops::Deref;

    let mut data = [1; 8];
    let mut buf = IoSliceMut::new(&mut data);

    // Mark 3 bytes as read.
    buf.advance(3);
    assert_eq!(buf.deref(), [1; 5].as_ref());
}
