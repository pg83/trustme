//@ compile-fail: Failed to infer type
trait Read<T> {
    fn value(&self) -> u8;
    fn again(&self) -> u8;
}
impl<T> Read<T> for u8 {
    fn value(&self) -> u8 { *self }
    fn again(&self) -> u8 { self.value() }
}
fn main() { assert_eq!(<u8 as Read<u16>>::again(&7), 7); }
