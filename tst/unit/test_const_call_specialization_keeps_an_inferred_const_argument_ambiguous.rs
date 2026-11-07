//@ compile-fail: error:
#![feature(generic_const_exprs, generic_arg_infer, specialization)]
#![allow(incomplete_features)]

struct Measure<T>(std::marker::PhantomData<T>);

impl<T> Measure<T> {
    const fn width() -> usize {
        std::mem::size_of::<T>()
    }
}

trait Code<T, const N: usize> {
    fn code() -> u32;
}

impl<T, const N: usize> Code<T, N> for () {
    default fn code() -> u32 {
        599
    }
}

impl<T> Code<T, { Measure::<T>::width() }> for ()
where
    [(); Measure::<T>::width()]:,
{
    fn code() -> u32 {
        601
    }
}

fn main() {
    let _: u32 = <() as Code<u16, _>>::code();
}
