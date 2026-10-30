struct Meter(u32);
impl Meter {
    fn read<T: Copy, const ADD: u32>(&self, value: T) -> (u32, T) { (self.0 + ADD, value) }
    fn advance<const STEP: u32>(&mut self) -> u32 { self.0 += STEP; self.0 }
}
trait Scale { fn combine<T: Into<u32>>(&self, value: T) -> u32; }
impl Scale for Meter {
    fn combine<T: Into<u32>>(&self, value: T) -> u32 { self.0 + value.into() }
}
fn main() {
    let mut meter = Box::new(Meter(4));
    assert_eq!(meter.advance::<{2 + 1}>(), 7);
    assert_eq!(meter.read::<u16, {1 + 1}>(9), (9, 9));
    assert_eq!(meter.combine::<u8>(5), 12);
}
