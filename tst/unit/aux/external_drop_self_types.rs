use std::cell::Cell;
pub struct Resource<'a, const N: u8>(pub &'a Cell<u8>);
impl<const N: u8> Drop for Resource<'_, N> {
    fn drop(&mut self) { self.0.set(self.0.get() + N); }
}
