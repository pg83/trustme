//@ edition: 2021
//@ crate-type: lib
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

trait Erase {
    type Value;
}
impl<T> Erase for T {
    type Value = ();
}
struct Wrap<T: Erase>(T::Value);

fn transfer<const N: usize>(
    value: Wrap<[u8; N.wrapping_add(1)]>,
) -> Wrap<[u8; N.wrapping_add(1)]>
where
    [(); N.wrapping_add(1)]:,
{
    value
}
