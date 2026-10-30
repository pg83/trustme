trait Pick<T> {
    fn take(&self, value: T) -> usize;
    fn integer(&self) -> usize;
    fn float(&self) -> usize;
}
impl<T> Pick<T> for u8 {
    fn take(&self, _: T) -> usize { std::mem::size_of::<T>() }
    fn integer(&self) -> usize { self.take(0) }
    fn float(&self) -> usize { self.take(0.0) }
}
fn main() {
    assert_eq!(<u8 as Pick<u16>>::integer(&7), 4);
    assert_eq!(<u8 as Pick<u16>>::float(&7), 8);
}
