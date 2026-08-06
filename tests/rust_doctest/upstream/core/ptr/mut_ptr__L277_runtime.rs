// Extracted from library/core/src/ptr/mut_ptr.rs:277
#![allow(unused)]
fn main() {
    Null-unchecked version
    
    If you are sure the pointer can never be null and are looking for some kind of
    `as_ref_unchecked` that returns the `&T` instead of `Option<&T>`, know that you can
    dereference the pointer directly.
}
