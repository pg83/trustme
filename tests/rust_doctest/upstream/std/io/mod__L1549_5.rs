// Extracted from library/std/src/io/mod.rs:1549
#![allow(unused)]
fn main() {
    use std::io::IoSlice;
    use std::ops::Deref;
    
    let buf1 = [1; 8];
    let buf2 = [2; 16];
    let buf3 = [3; 8];
    let mut bufs = &mut [
        IoSlice::new(&buf1),
        IoSlice::new(&buf2),
        IoSlice::new(&buf3),
    ][..];
    
    // Mark 10 bytes as written.
    IoSlice::advance_slices(&mut bufs, 10);
    assert_eq!(bufs[0].deref(), [2; 14].as_ref());
    assert_eq!(bufs[1].deref(), [3; 8].as_ref());
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    Get the underlying bytes as a slice with the original lifetime.
    
    This doesn't borrow from `self`, so is less restrictive than calling
    `.deref()`, which does.
    
    Examples
}
