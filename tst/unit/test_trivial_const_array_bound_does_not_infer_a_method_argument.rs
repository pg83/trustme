//@ compile-fail: Failed to infer const argument
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]
trait Layout<const N: usize> { fn width(&self) -> usize; fn relay(&self) -> usize; }
struct Wire;
impl<const N: usize> Layout<N> for Wire where [u8; N]: Sized {
    fn width(&self) -> usize { N }
    fn relay(&self) -> usize { self.width() }
}
fn main() { assert_eq!(<Wire as Layout<3>>::relay(&Wire), 3); assert_eq!(<Wire as Layout<7>>::relay(&Wire), 7); }
