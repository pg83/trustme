// Extracted from library/core/src/ptr/mut_ptr.rs:626
#![allow(unused)]
fn main() {
    Returns a unique reference to the value behind the pointer.
    If the pointer may be null or the value may be uninitialized, [`as_uninit_mut`] must be used instead.
    If the pointer may be null, but the value is known to have been initialized, [`as_mut`] must be used instead.
    
    For the shared counterpart see [`as_ref_unchecked`].
    
    [`as_mut`]: #method.as_mut
    [`as_uninit_mut`]: #method.as_uninit_mut
    [`as_ref_unchecked`]: #method.as_mut_unchecked
    
    Safety
    
    When calling this method, you have to ensure that
    the pointer is [convertible to a reference](crate::ptr#pointer-to-reference-conversion).
    
    Examples
}
