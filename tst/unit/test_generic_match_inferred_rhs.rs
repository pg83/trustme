trait Code { fn code(&self) -> u8; }
impl Code for i16 { fn code(&self) -> u8 { 16 } }
impl Code for f64 { fn code(&self) -> u8 { 64 } }
impl Code for bool { fn code(&self) -> u8 { 1 } }
struct Wrap<T>(T);
impl Code for Wrap<u32> { fn code(&self) -> u8 { 32 } }
trait Same<T> { fn agrees(&self, value: T) -> bool; }
impl<T: PartialEq> Same<T> for (T, T) {
    fn agrees(&self, value: T) -> bool { self.0 == value && self.1 == value }
}
fn main() {
    let integer = 7;
    let floating = 1.25;
    assert_eq!(integer.code(), 16);
    assert_eq!(floating.code(), 64);
    assert_eq!(true.code(), 1);
    let _: i16 = integer;
    let _: f64 = floating;
    let inferred = Wrap(Default::default());
    assert_eq!(inferred.code(), 32);
    assert_eq!(inferred.0, 0);
    assert!((9, 9).agrees(9u16));
    assert!(!(9, 10).agrees(9u16));
}
