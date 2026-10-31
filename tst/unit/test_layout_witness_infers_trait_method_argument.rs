#![feature(generic_const_exprs)]
#![allow(incomplete_features)]
use std::mem::size_of;
trait Layout<T> { fn width(&self) -> usize; fn relay(&self) -> usize; }
struct Wire;
impl<T> Layout<T> for Wire where [u8; size_of::<T>()]: Sized {
    fn width(&self) -> usize { size_of::<T>() }
    fn relay(&self) -> usize { self.width() }
}
fn main() {
    assert_eq!(<Wire as Layout<u16>>::relay(&Wire), 2);
    assert_eq!(<Wire as Layout<[u8; 5]>>::relay(&Wire), 5);
}
