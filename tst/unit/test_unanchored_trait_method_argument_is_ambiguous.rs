//@ compile-fail: Failed to infer type
use std::mem::size_of;
trait Layout<T> { fn width(&self) -> usize; fn relay(&self) -> usize; }
struct Wire;
impl<T> Layout<T> for Wire {
    fn width(&self) -> usize { size_of::<T>() }
    fn relay(&self) -> usize { self.width() }
}
fn main() {
    assert_eq!(<Wire as Layout<u16>>::relay(&Wire), 2);
    assert_eq!(<Wire as Layout<[u8; 5]>>::relay(&Wire), 5);
}
