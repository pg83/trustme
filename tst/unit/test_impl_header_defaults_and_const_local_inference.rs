trait Code<T = u8> { fn code(&self) -> T; }
struct Wrap<'a, T = u8>(&'a T);
impl Code for Wrap<'_> { fn code(&self) -> u8 { *self.0 } }
impl Code for [u8; { let n: _ = 3usize; n }] {
    fn code(&self) -> u8 { self[2] }
}
impl<T> Wrap<'_, T> where T: { fn value(&self) -> &T { self.0 } }
struct Slots<const N: usize>([u8; N]);
impl<const N: usize> Slots<N> { fn length(&self) -> usize { self.0.len() } }
fn main() {
    assert_eq!(Slots([4u8, 5]).length(), 2);
    assert_eq!(Wrap(&7).code(), 7);
    assert_eq!(*Wrap(&9u16).value(), 9);
    assert_eq!([1, 2, 9].code(), 9);
}
