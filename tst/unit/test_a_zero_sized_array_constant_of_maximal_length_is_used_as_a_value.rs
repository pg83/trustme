use std::marker::PhantomData;
use std::mem::MaybeUninit;

struct Make<T, const N: usize>(PhantomData<T>);

impl<T, const N: usize> Make<T, N> {
    const VALUE: MaybeUninit<T> = MaybeUninit::uninit();
    const ARRAY: [MaybeUninit<T>; N] = [Self::VALUE; N];
}

struct Holder<T, const N: usize> {
    xs: [MaybeUninit<T>; N],
    len: usize,
}

fn make<T, const N: usize>() -> Holder<T, N> {
    Holder { xs: Make::ARRAY, len: 0 }
}

fn main() {
    let h: Holder<(), { usize::MAX }> = make();
    assert_eq!(std::mem::size_of_val(&h.xs), 0);
    assert_eq!(h.len, 0);
    let plain: [(); usize::MAX] = [(); usize::MAX];
    assert_eq!(std::mem::size_of_val(&plain), 0);
}
