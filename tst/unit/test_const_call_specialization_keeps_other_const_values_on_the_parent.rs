#![feature(generic_const_exprs, specialization)]
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
        579
    }
}

impl<T> Code<T, { Measure::<T>::width() }> for ()
where
    [(); Measure::<T>::width()]:,
{
    fn code() -> u32 {
        587
    }
}

fn main() {
    assert_eq!(<() as Code<u16, 2>>::code(), 587);
    assert_eq!(<() as Code<u16, 3>>::code(), 579);
    assert_eq!(<() as Code<u32, 4>>::code(), 587);
}
