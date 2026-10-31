trait Read { type Item; type Error; fn read(&self) -> Result<Self::Item, Self::Error>; }
impl Read for u8 {
    type Item = u16;
    type Error = u32;
    fn read(&self) -> Result<u16, u32> { if *self == 0 { Err(99) } else { Ok(*self as u16 + 100) } }
}
fn first<T, E>(values: &[&dyn Read<Item = T, Error = E>]) -> Result<T, E> { values[0].read() }
fn main() {
    let value = 7u8;
    let empty = 0u8;
    let values: [&dyn Read<Item = _, Error = _>; 2] = [&value, &empty];
    assert_eq!(first(&values), Ok(107u16));
    assert_eq!(first(&values[1..]), Err(99u32));
}
