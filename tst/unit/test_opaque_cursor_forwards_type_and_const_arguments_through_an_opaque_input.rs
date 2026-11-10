#![feature(type_alias_impl_trait)]

type Cursor<T, const N: usize> = impl Iterator<Item = (T, [u8; N])>;

#[define_opaque(Cursor)]
fn make_cursor<T, const N: usize>(value: T, bytes: [u8; N]) -> Cursor<T, N> {
    core::iter::once((value, bytes))
}

fn forward<T, const N: usize>(value: T, bytes: [u8; N]) -> Cursor<T, N> {
    make_cursor(value, bytes)
}

fn take_one<const N: usize>(mut values: impl Iterator<Item = (u16, [u8; N])>) -> (u16, [u8; N]) {
    let first = values.next().unwrap();
    assert!(values.next().is_none());
    first
}

fn main() {
    assert_eq!(take_one(forward(71u16, [2, 3, 5])), (71, [2, 3, 5]));
    let mut empty_array = forward(true, []);
    assert_eq!(empty_array.next(), Some((true, [])));
    assert_eq!(empty_array.next(), None);
}
