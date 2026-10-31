#![feature(generic_const_exprs)]
#![allow(incomplete_features)]
trait Width { const N: usize; }
impl Width for u16 { const N: usize = 2; }
impl Width for [u8; 5] { const N: usize = 5; }
trait Layout<T> { fn width(&self) -> usize; fn relay(&self) -> usize; }
struct Wire;
impl<T: Width> Layout<T> for Wire where [u8; T::N + 1]: Sized {
    fn width(&self) -> usize { T::N }
    fn relay(&self) -> usize { self.width() }
}
fn main() { assert_eq!(<Wire as Layout<u16>>::relay(&Wire), 2); assert_eq!(<Wire as Layout<[u8; 5]>>::relay(&Wire), 5); }
