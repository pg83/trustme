// Extracted from library/std/src/io/mod.rs:1390
#![allow(unused)]
fn main() {
    use std::io::IoSliceMut;
    use std::ops::Deref;
    
    let mut buf1 = [1; 8];
    let mut buf2 = [2; 16];
    let mut buf3 = [3; 8];
    let mut bufs = &mut [
        IoSliceMut::new(&mut buf1),
        IoSliceMut::new(&mut buf2),
        IoSliceMut::new(&mut buf3),
    ][..];
    
    // Mark 10 bytes as read.
    IoSliceMut::advance_slices(&mut bufs, 10);
    assert_eq!(bufs[0].deref(), [2; 14].as_ref());
    assert_eq!(bufs[1].deref(), [3; 8].as_ref());
}
