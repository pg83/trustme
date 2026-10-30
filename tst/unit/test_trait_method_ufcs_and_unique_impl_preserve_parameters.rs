trait Read<T> { fn value(&self) -> usize; fn again(&self) -> usize; }
impl<T> Read<T> for u8 {
    fn value(&self) -> usize { *self as usize + std::mem::size_of::<T>() }
    fn again(&self) -> usize { <Self as Read<T>>::value(self) }
}
trait Unique<T> { fn value(&self) -> usize; fn again(&self) -> usize; }
impl Unique<u32> for u16 {
    fn value(&self) -> usize { *self as usize + 4 }
    fn again(&self) -> usize { self.value() }
}
fn main() {
    assert_eq!(<u8 as Read<u16>>::again(&7), 9);
    assert_eq!(<u8 as Read<u64>>::again(&7), 15);
    assert_eq!(<u16 as Unique<u32>>::again(&7), 11);
}
