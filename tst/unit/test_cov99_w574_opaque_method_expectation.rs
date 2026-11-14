//@ crate-type: lib
#![feature(type_alias_impl_trait)]

use core::marker::PhantomData;

pub type Callback<T> = impl Fn(T) -> usize;

struct Gate<T>(PhantomData<T>);

impl<T> Gate<T> {
    fn accept(&self, callback: Callback<T>) -> Callback<T> {
        callback
    }
}

#[define_opaque(Callback)]
pub fn make<T>() -> Callback<T> {
    Gate(PhantomData).accept(|_: T| core::mem::size_of::<T>())
}
