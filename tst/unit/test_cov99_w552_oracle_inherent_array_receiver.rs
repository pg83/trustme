//@ crate-type: lib
//@ env: TRUSTME_CAPS_ORACLE=1
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

struct Holder<U>(core::marker::PhantomData<U>);

impl<U> Holder<U> {
    const WIDTH: usize = 1;
}

pub fn probe<T>()
where
    [(); core::mem::size_of::<&T>()]:,
{
    let _ = Holder::<[u8; core::mem::size_of::<&T>()]>::WIDTH;
}
