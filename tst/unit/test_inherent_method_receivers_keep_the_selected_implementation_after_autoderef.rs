struct Choice<T>(T);
impl Choice<u8> { fn read(&self) -> u16 { self.0 as u16 + 1 } }
impl Choice<u16> { fn read(&self) -> u16 { self.0 + 5 } }
fn main() {
    let small = Box::new(Choice(12_u8));
    let wide = Box::new(Choice(32_u16));
    assert_eq!(small.read(), 13);
    assert_eq!((&wide).read(), 37);
}
