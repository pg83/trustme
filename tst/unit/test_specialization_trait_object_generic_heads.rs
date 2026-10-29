#![feature(specialization)]
trait Read<T> { fn read(&self) -> T; }
impl Read<u8> for u8 { fn read(&self) -> u8 { *self } }
impl Read<u16> for u16 { fn read(&self) -> u16 { *self } }
trait Tag { fn tag(&self) -> u8; }
impl<T> Tag for dyn Read<T> + Send {
    default fn tag(&self) -> u8 { 3 }
}
impl Tag for dyn Read<u8> + Send {
    fn tag(&self) -> u8 { 7 }
}
fn main() {
    let narrow: &(dyn Read<u8> + Send) = &11u8;
    let wide: &(dyn Read<u16> + Send) = &19u16;
    assert_eq!(narrow.tag(), 7);
    assert_eq!(wide.tag(), 3);
    assert_eq!(narrow.read(), 11);
    assert_eq!(wide.read(), 19);
}
