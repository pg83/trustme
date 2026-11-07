#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

struct Measure<T>(std::marker::PhantomData<T>);

impl<T> Measure<T> {
    const fn width() -> usize {
        std::mem::size_of::<T>()
    }
}

trait Select<T, const N: usize> {
    type Output;
}

impl<T> Select<T, { Measure::<T>::width() }> for ()
where
    [(); Measure::<T>::width()]:,
{
    type Output = u32;
}

fn main() {
    let value: <() as Select<u16, 2>>::Output = 557u32;
    assert_eq!(value, 557);
    assert_eq!(std::mem::size_of_val(&value), 4);
}
