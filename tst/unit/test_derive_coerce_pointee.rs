#![feature(derive_coerce_pointee)]

use std::marker::CoercePointee;

trait Value {
    fn get(&self) -> u32;
}

impl Value for u32 {
    fn get(&self) -> u32 {
        *self
    }
}

#[derive(CoercePointee)]
#[repr(transparent)]
struct Pointer<'a, #[pointee] T: ?Sized>(&'a T);

fn main() {
    let value = 7;
    let thin = Pointer(&value);
    let wide: Pointer<'_, dyn Value> = thin;
    assert_eq!(wide.0.get(), 7);
}
