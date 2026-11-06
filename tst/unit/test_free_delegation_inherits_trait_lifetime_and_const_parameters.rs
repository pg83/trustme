#![feature(fn_delegation)]
#![allow(incomplete_features)]

trait Window<'a, const N: usize>: Sized {
    fn head<T: ?Sized>(value: &'a Self, _: &'a T, _: [u8; N]) -> &'a Self
    where Self: 'a {
        value
    }
}
impl<'a, const N: usize> Window<'a, N> for u16 {}

reuse Window::head;

fn main() {
    let value = 31u16;
    assert_eq!(*head(&value, "tail", [7, 9, 11]), 31);
}
