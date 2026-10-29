#![feature(specialization)]
trait Scalar {}
impl Scalar for u8 {}
impl Scalar for i32 {}
trait Stamp { type Bytes; fn stamp(self) -> Self::Bytes; }
impl<T: Scalar> Stamp for [T; 1] {
    type Bytes = u16;
    default fn stamp(self) -> u16 { 0 }
}
impl Stamp for [u8; 1] { fn stamp(self) -> u16 { self[0] as u16 + 1 } }
impl Stamp for [i32; 1] { fn stamp(self) -> u16 { self[0] as u16 + 2 } }
fn check<T>(expected: <[T; 1] as Stamp>::Bytes, value: T) -> <[T; 1] as Stamp>::Bytes
where [T; 1]: Stamp, <[T; 1] as Stamp>::Bytes: PartialEq + std::fmt::Debug {
    let actual = [value].stamp();
    assert_eq!(actual, expected);
    actual
}
fn main() {
    let result: u16 = check(21u16, 19);
    assert_eq!(result, 21);
    assert_eq!(check(20u16, 19u8), 20);
}
