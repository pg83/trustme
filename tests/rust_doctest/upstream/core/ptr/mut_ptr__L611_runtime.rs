// Extracted from library/core/src/ptr/mut_ptr.rs:611
#![allow(unused)]
fn main() {
    Null-unchecked version
    
    If you are sure the pointer can never be null and are looking for some kind of
    `as_mut_unchecked` that returns the `&mut T` instead of `Option<&mut T>`, know that
    you can dereference the pointer directly.
}
