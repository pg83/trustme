//@ compile-fail: error:
#![feature(generic_const_exprs, specialization)]
#![allow(incomplete_features)]

struct Measure<T>(std::marker::PhantomData<T>);

impl<T> Measure<T> {
    const fn width() -> usize {
        std::mem::size_of::<T>()
    }
}

trait Value {
    type Number;
}

struct Plain;
struct Refined;

impl Value for Plain {
    type Number = u16;
}

impl Value for Refined {
    type Number = u32;
}

trait Choose<T, const A: usize, const B: usize> {
    type Selected: Value;
}

impl<T, const N: usize> Choose<T, N, N> for () {
    default type Selected = Plain;
}

impl<T> Choose<T, { Measure::<T>::width() }, { Measure::<T>::width() }> for ()
where
    [(); Measure::<T>::width()]:,
{
    type Selected = Refined;
}

type Result = <<() as Choose<u16, 2, 2>>::Selected>::Number;

fn main() {
    let value: Result = 547u32;
    assert_eq!(value, 547);
    assert_eq!(std::mem::size_of::<Result>(), 4);
}
