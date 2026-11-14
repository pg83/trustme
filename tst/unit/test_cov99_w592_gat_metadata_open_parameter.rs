//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort
#![feature(ptr_metadata)]

use std::marker::PhantomData;

trait Family {
    type Tail<U>: ?Sized;
}

fn tail<F: Family, U>(_: PhantomData<U>) -> *const F::Tail<U> {
    loop {}
}

pub fn probe<F: Family>()
where
    F::Tail<u8>: Sized,
{
    let tag = PhantomData;
    let pointer = tail::<F, _>(tag);
    let _: () = std::ptr::metadata(pointer);
    let _: PhantomData<u8> = tag;
}
