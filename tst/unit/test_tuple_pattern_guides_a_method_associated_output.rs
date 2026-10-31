trait Produce { type Output; fn produce(self) -> Self::Output; }
impl Produce for u8 {
    type Output = (u16, u32);
    fn produce(self) -> Self::Output { (self as u16 + 100, self as u32 + 200) }
}
fn main() {
    let (first, second) = 23u8.produce();
    assert_eq!(first, 123u16);
    assert_eq!(second, 223u32);
}
