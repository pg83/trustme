//@ compile-flags: -Zmir-opt-level=0
#![feature(type_alias_impl_trait)]

type Sequence<T> = impl Iterator<Item = T>;

#[define_opaque(Sequence)]
fn sequence<T>(value: T) -> Sequence<T> { std::iter::once(value) }

#[define_opaque(Sequence)]
fn another<T>(value: T) -> Sequence<T> { std::iter::once(value) }

fn main() {
    let values: Vec<i8> = sequence(-128).collect();
    assert_eq!(values, vec![i8::MIN]);
    let values: Vec<i128> = sequence(-170141183460469231731687303715884105728).collect();
    assert_eq!(values, vec![i128::MIN]);
    assert_eq!(another(7).next(), Some(7));
}
