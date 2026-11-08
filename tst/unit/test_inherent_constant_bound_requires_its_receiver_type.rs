//@ compile-fail: error
use std::marker::PhantomData;

trait Width {}
impl Width for u8 {}
impl Width for u16 {}

struct Measure<T>(PhantomData<T>);

impl<T: Width> Measure<T> {
    const BYTES: usize = std::mem::size_of::<T>();
}

fn main() {
    let _ = Measure::<_>::BYTES;
}
