// Extracted from library/core/src/marker.rs:1171
#![feature(derive_coerce_pointee)]
use std::marker::CoercePointee;
use std::ops::Deref;

#[derive(CoercePointee)]
#[repr(transparent)]
struct MySmartPointer<T: ?Sized>(Box<T>);

impl<T: ?Sized> Deref for MySmartPointer<T> {
    type Target = T;
    fn deref(&self) -> &T {
        &self.0
    }
}

trait MyTrait {}

impl MyTrait for i32 {}

fn main() {
    let ptr: MySmartPointer<i32> = MySmartPointer(Box::new(4));

    // This coercion would be an error without the derive.
    let ptr: MySmartPointer<dyn MyTrait> = ptr;
}
