// Extracted from library/core/src/ptr/non_null.rs:34
#![allow(unused)]
fn main() {
    use std::cell::Cell;
    use std::marker::PhantomData;
    struct Invariant<T> {
        ptr: std::ptr::NonNull<T>,
        _invariant: PhantomData<Cell<T>>,
    }
}
