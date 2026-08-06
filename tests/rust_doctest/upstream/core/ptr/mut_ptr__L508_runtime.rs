// Extracted from library/core/src/ptr/mut_ptr.rs:508
#![allow(unused)]
fn main() {
    Adds a signed offset in bytes to a pointer using wrapping arithmetic.
    
    `count` is in units of **bytes**.
    
    This is purely a convenience for casting to a `u8` pointer and
    using [wrapping_offset][pointer::wrapping_offset] on it. See that method
    for documentation.
    
    For non-`Sized` pointees this operation changes only the data pointer,
    leaving the metadata untouched.
    
    
    
    
    
    
    
    
    Masks out bits of the pointer according to a mask.
    
    This is convenience for `ptr.map_addr(|a| a & mask)`.
    
    For non-`Sized` pointees this operation changes only the data pointer,
    leaving the metadata untouched.
    
    # Examples
}
