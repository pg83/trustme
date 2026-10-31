trait Value { type Item; fn value(&self) -> Self::Item; }
impl Value for u16 { type Item = u32; fn value(&self) -> u32 { *self as u32 + 3 } }
fn read<T>(slot: &mut (&mut dyn Value<Item = T>, u8)) -> T { slot.0.value() }
fn main() {
    let mut value = 23u16;
    let object: &mut dyn Value<Item = _> = &mut value;
    let mut slot = (object, 7u8);
    assert_eq!(read(&mut slot), 26u32);
    assert_eq!(slot.1, 7);
}
