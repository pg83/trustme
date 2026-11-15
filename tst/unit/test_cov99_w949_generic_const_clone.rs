//@ compile-flags: -Cpanic=abort

struct Copier<T>(std::marker::PhantomData<T>);

impl<T: Clone> Copier<T> {
    const CLONE: fn(&(T,)) -> (T,) = <(T,) as Clone>::clone;
}

#[inline(never)]
fn duplicate<T: Clone>(value: &(T,)) -> (T,) {
    (Copier::<T>::CLONE)(value)
}

fn main() {
    assert_eq!(duplicate(&(17u32,)), (17u32,));
}
