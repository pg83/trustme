// ref-cast's layout check on an unsized struct: `Layout::<Self>::SIZE` is the
// inherent `impl<T> Layout<T>`'s constant only for a sized `T`; otherwise the
// imported `LayoutUnsized` supplies `usize::MAX`. Upstream's path probe
// drops an inherent candidate whose impl predicates cannot hold
// (`consider_probe`, `predicate_may_hold`), so the trait's constant is found
// for `str`, `[u8]` and an unsized parameter. We took the inherent constant
// and then failed on `T: Sized`.
use std::mem;

pub struct Layout<T: ?Sized>(T);

pub trait LayoutUnsized<T: ?Sized> {
    const SIZE: usize = usize::MAX;
}

impl<T: ?Sized> LayoutUnsized<T> for Layout<T> {}

impl<T> Layout<T> {
    pub const SIZE: usize = mem::size_of::<T>();
}

fn size<T: ?Sized>() -> usize
where
    Layout<T>: LayoutUnsized<T>,
{
    <Layout<T> as LayoutUnsized<T>>::SIZE
}

fn unsized_param<T: ?Sized>() -> usize {
    Layout::<T>::SIZE
}

fn main() {
    assert_eq!(Layout::<u32>::SIZE, 4);
    assert_eq!(Layout::<str>::SIZE, usize::MAX);
    assert_eq!(Layout::<[u8]>::SIZE, usize::MAX);
    assert_eq!(size::<str>(), usize::MAX);
    assert_eq!(unsized_param::<str>(), usize::MAX);
    assert_eq!(unsized_param::<u16>(), usize::MAX);
}
