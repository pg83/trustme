// Extracted from library/core/src/ptr/mut_ptr.rs:325
#![allow(unused)]
fn main() {
    Returns `None` if the pointer is null, or else returns a shared reference to
    the value wrapped in `Some`. In contrast to [`as_ref`], this does not require
    that the value has to be initialized.
    
    For the mutable counterpart see [`as_uninit_mut`].
    
    [`as_ref`]: pointer#method.as_ref-1
    [`as_uninit_mut`]: #method.as_uninit_mut
    
    Safety
    
    When calling this method, you have to ensure that *either* the pointer is null *or*
    the pointer is [convertible to a reference](crate::ptr#pointer-to-reference-conversion).
    Note that because the created reference is to `MaybeUninit<T>`, the
    source pointer can point to uninitialized memory.
    
    Panics during const evaluation
    
    This method will panic during const evaluation if the pointer cannot be
    determined to be null or not. See [`is_null`] for more information.
    
    [`is_null`]: #method.is_null-1
    
    Examples
}
