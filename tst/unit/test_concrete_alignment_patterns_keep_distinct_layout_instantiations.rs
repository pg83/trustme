//@ compile-variant: -O
use std::marker::PhantomData;
struct Layout<T>(PhantomData<T>);
impl<T> Layout<T> { const ALIGNMENT: usize = std::mem::align_of::<T>(); }
#[repr(align(16))]
struct Aligned(u8);
fn classify(value: usize) -> u8 {
    match value { Layout::<u8>::ALIGNMENT => 1, Layout::<u32>::ALIGNMENT => 2, Layout::<Aligned>::ALIGNMENT => 3, _ => 4 }
}
fn main() {
    assert_eq!(classify(1), 1);
    assert_eq!(classify(4), 2);
    assert_eq!(classify(16), 3);
    assert_eq!(classify(8), 4);
    assert_eq!(Aligned(23).0, 23);
}
