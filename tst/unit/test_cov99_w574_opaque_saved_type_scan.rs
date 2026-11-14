//@ crate-type: lib
#![feature(type_alias_impl_trait, generic_const_exprs)]
#![allow(incomplete_features)]

use core::marker::PhantomData;

pub trait Width { const N: usize; }
impl<T> Width for T {
    const N: usize = {
        let marker: *const T = 0 as *const T;
        let _ = marker;
        core::mem::size_of::<T>()
    };
}

pub type Callback<T>
where [(); <T as Width>::N]:,
= impl Fn([T; <T as Width>::N]) -> usize;

struct Gate<T>(PhantomData<T>);
impl<T> Gate<T> {
    fn accept(&self, callback: Callback<T>) -> Callback<T>
    where [(); <T as Width>::N]:,
    { callback }
}

#[define_opaque(Callback)]
pub fn make<T>() -> Callback<T>
where [(); <T as Width>::N]:,
{
    Gate(PhantomData).accept(|items: [T; <T as Width>::N]| items.len())
}
