// Extracted from library/core/src/marker.rs:1280
#![allow(unused)]
#![feature(derive_coerce_pointee)]
fn main() {
    use std::marker::{CoercePointee, PhantomData};
    #[derive(CoercePointee)]
    #[repr(transparent)]
    struct MySmartPointer<#[pointee] T: ?Sized, U> {
        ptr: Box<T>,
        _phantom: PhantomData<U>,
    }
}
