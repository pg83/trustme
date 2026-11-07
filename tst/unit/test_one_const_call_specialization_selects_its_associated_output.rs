#![feature(generic_const_exprs, specialization)]
#![allow(incomplete_features)]

struct Measure<T>(std::marker::PhantomData<T>);

impl<T> Measure<T> {
    const fn width() -> usize {
        std::mem::size_of::<T>()
    }
}

trait Pick<T, const N: usize> {
    type Output;
}

impl<T, const N: usize> Pick<T, N> for () {
    default type Output = u16;
}

impl<T> Pick<T, { Measure::<T>::width() }> for ()
where
    [(); Measure::<T>::width()]:,
{
    type Output = u32;
}

fn main() {
    let value: <() as Pick<u16, 2>>::Output = 563u32;
    assert_eq!(value, 563);
    assert_eq!(std::mem::size_of_val(&value), 4);
}
