//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort
//@ compile-fail: type annotations needed
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
    F::Tail<u16>: Sized,
{
    let pointer = tail::<F, _>(PhantomData);
    let _: () = std::ptr::metadata(pointer);
}
