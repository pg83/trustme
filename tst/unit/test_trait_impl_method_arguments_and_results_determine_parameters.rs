trait Pick<T: Default> {
    fn take(&self, value: T) -> usize;
    fn make(&self) -> T;
    fn explicit(&self) -> usize;
    fn from_result(&self) -> u32;
}
impl<T: Default> Pick<T> for u8 {
    fn take(&self, _: T) -> usize { std::mem::size_of::<T>() }
    fn make(&self) -> T { T::default() }
    fn explicit(&self) -> usize { self.take(0u64) }
    fn from_result(&self) -> u32 { self.make() }
}
fn main() {
    assert_eq!(<u8 as Pick<u16>>::explicit(&7), 8);
    assert_eq!(<u8 as Pick<u16>>::from_result(&7), 0);
}
