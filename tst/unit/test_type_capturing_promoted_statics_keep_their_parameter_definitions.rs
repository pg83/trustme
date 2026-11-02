//@ compile-variant: -O
struct Holder<T>(core::marker::PhantomData<T>);
impl<T> Holder<T> {
    const SIZE: usize = core::mem::size_of::<T>();
    const REFERENCE: &'static usize = &Self::SIZE;
}
fn main() {
    let first = Holder::<u16>::REFERENCE;
    let second = Holder::<[u8; 3]>::REFERENCE;
    assert_eq!((*first, *second), (2, 3));
    assert_eq!(*first, 2);
}
