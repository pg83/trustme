//@ compile-fail: type annotations needed: cannot infer a type satisfying
trait Read<T> {
    fn value(&self) -> u8;
    fn again(&self) -> u8;
}
impl Read<u16> for u8 {
    fn value(&self) -> u8 { *self }
    fn again(&self) -> u8 { self.value() }
}
fn main() { assert_eq!(<u8 as Read<u16>>::again(&7), 7); }
impl Read<u32> for u8 {
    fn value(&self) -> u8 { *self }
    fn again(&self) -> u8 { self.value() }
}
