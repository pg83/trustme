// Extracted from library/core/src/ptr/mut_ptr.rs:292
#![allow(unused)]
fn main() {
    Returns a shared reference to the value behind the pointer.
    If the pointer may be null or the value may be uninitialized, [`as_uninit_ref`] must be used instead.
    If the pointer may be null, but the value is known to have been initialized, [`as_ref`] must be used instead.
    
    For the mutable counterpart see [`as_mut_unchecked`].
    
    [`as_ref`]: #method.as_ref
    [`as_uninit_ref`]: #method.as_uninit_ref
    [`as_mut_unchecked`]: #method.as_mut_unchecked
    
    Safety
    
    When calling this method, you have to ensure that the pointer is [convertible to a reference](crate::ptr#pointer-to-reference-conversion).
    
    Examples
}
