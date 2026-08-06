// Extracted from library/std/src/io/mod.rs:1517
#![allow(unused)]
fn main() {
    use std::io::IoSlice;
    use std::ops::Deref;
    
    let data = [1; 8];
    let mut buf = IoSlice::new(&data);
    
    // Mark 3 bytes as read.
    buf.advance(3);
    assert_eq!(buf.deref(), [1; 5].as_ref());
}
