trait Measure { fn measure(&self) -> u16; }
impl Measure for u8 { fn measure(&self) -> u16 { *self as u16 } }
impl Measure for Box<u8> { fn measure(&self) -> u16 { 200 + **self as u16 } }
fn total<T: Measure>(values: [&T; 2]) -> u16 { values[0].measure() + values[1].measure() }
fn main() {
    let plain = 11u8;
    let boxed = Box::new(13u8);
    assert_eq!(total([&plain, &boxed]), 24);
}
