//@ compile-fail: error:
#![feature(type_alias_impl_trait)]

type Batch<T: Clone> = impl Clone;
type Bundle<T: Clone> = impl Clone;

#[define_opaque(Batch)]
fn batch<T: Clone>(value: T) -> Batch<T> { value }
#[define_opaque(Bundle)]
fn bundle<T: Clone>(value: T) -> Bundle<T> { value }

fn main() {
    let _ = batch(227u16);
    let other = bundle(229u16);
    let _ = <Batch<_> as Clone>::clone(&other);
}
