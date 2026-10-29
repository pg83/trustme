#![feature(impl_trait_in_assoc_type)]
trait Factory {
    type Value: Copy + AsRef<[u16]>;
    fn a_skip(value: u8) -> u8;
    fn make() -> Self::Value;
}
impl Factory for () {
    type Value = impl Copy + AsRef<[u16]>;
    fn a_skip(value: u8) -> u8 { value + 2 }
    fn make() -> Self::Value { [7u16, 11, 13] }
}
const WIDTH: usize = std::mem::size_of::<<() as Factory>::Value>();
const ALIGN: usize = std::mem::align_of::<<() as Factory>::Value>();
fn main() {
    assert_eq!(WIDTH, 6);
    assert_eq!(ALIGN, 2);
    let value = <() as Factory>::make();
    assert_eq!(value.as_ref(), &[7, 11, 13]);
    assert_eq!(<() as Factory>::a_skip(3), 5);
}
