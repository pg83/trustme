//@ crate-type: lib
#![feature(type_alias_impl_trait)]

type Callback<const N: usize> = impl Fn([u8; N]) -> [u8; N];

trait Accept<const N: usize> {
    fn accept(&self, callback: Callback<N>) -> Callback<N> {
        callback
    }
}

struct Sink;
impl<const N: usize> Accept<N> for Sink {}

#[define_opaque(Callback)]
pub fn make<const N: usize>() -> Callback<N> {
    Sink.accept(|value: [u8; N]| value)
}
