// Extracted from library/core/src/ptr/mut_ptr.rs:572
#![allow(unused)]
fn main() {
    Returns `None` if the pointer is null, or else returns a unique reference to
    the value wrapped in `Some`. If the value may be uninitialized, [`as_uninit_mut`]
    must be used instead.
    
    For the shared counterpart see [`as_ref`].
    
    [`as_uninit_mut`]: #method.as_uninit_mut
    [`as_ref`]: pointer#method.as_ref-1
    
    Safety
    
    When calling this method, you have to ensure that *either*
    the pointer is null *or*
    the pointer is [convertible to a reference](crate::ptr#pointer-to-reference-conversion).
    
    Panics during const evaluation
    
    This method will panic during const evaluation if the pointer cannot be
    determined to be null or not. See [`is_null`] for more information.
    
    [`is_null`]: #method.is_null-1
    
    Examples
}
