//@ compile-variant: -O
#![feature(generic_const_items)]
#![allow(incomplete_features)]

struct Rows<T>(std::marker::PhantomData<T>);
impl<T: Copy + 'static> Rows<T> {
    const ROW<const N: usize>: &'static [Option<T>; N] = &[None; N];
}

#[inline(never)]
fn row<T: Copy + 'static, const N: usize>() -> &'static [Option<T>; N] {
    Rows::<T>::ROW::<N>
}

fn main() {
    assert_eq!(Rows::<u16>::ROW::<2>, &[None, None]);
    assert_eq!(row::<u32, 3>(), &[None, None, None]);
}
