//@ compile-variant: -O
#![feature(type_alias_impl_trait)]

type Batch<T: Clone, const N: usize> = impl Clone;

#[define_opaque(Batch)]
fn batch<T: Clone, const N: usize>(values: [T; N]) -> Batch<T, N> { values }

fn main() {
    let words = batch([193u16, 197u16, 199u16]);
    let copied = <Batch<_, 3> as Clone>::clone(&words);
    assert_eq!(std::mem::size_of_val(&copied), 6);
    let bytes = batch([211u8, 223u8]);
    let copied = <Batch<_, 2> as Clone>::clone(&bytes);
    assert_eq!(std::mem::size_of_val(&copied), 2);
}
