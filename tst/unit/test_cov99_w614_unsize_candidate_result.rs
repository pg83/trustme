//@ crate-type: lib
#![feature(unsize)]

use std::marker::{PhantomData, Unsize};

struct Array<T>(PhantomData<T>);
trait Emits<R> {}
impl<'a, T: 'a> Emits<&'a [T; 2]> for Array<T> {}

fn emit<T, R>() -> R
where
    Array<T>: Emits<R>,
{
    loop {}
}

pub fn probe<S: ?Sized>()
where
    [u8; 2]: Unsize<S>,
{
    let value = emit();
    let _: &S = value;
}
