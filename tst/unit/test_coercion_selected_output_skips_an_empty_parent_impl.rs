#![feature(specialization)]
#![allow(incomplete_features)]
trait Read { type Output: Default; fn code() -> u16; }
impl<T: ?Sized> Read for &T { default type Output = u16; default fn code() -> u16 { 1 } }
impl<T: Copy> Read for &T { default fn code() -> u16 { 2 } }
impl Read for &[u8; 2] { fn code() -> u16 { 3 } }
fn output<V: Read>(_: &[V]) -> (V::Output, u16) { (Default::default(), V::code()) }
fn main() {
    let mut left = [2u8, 3];
    let right = [5u8, 7];
    let (value, code) = output(&[&right, &mut left]);
    assert_eq!(std::mem::size_of_val(&value), 2);
    assert_eq!(code, 3);
}
