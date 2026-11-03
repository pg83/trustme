#![feature(pattern_types, pattern_type_macro)]
macro_rules! retain { ($name:ident, $ty:ty) => {
    type $name = $ty;
    const SPELLING: &str = stringify!($ty);
}; }
retain!(Window, std::pat::pattern_type!(u16 is 5..=13));
fn main() {
    let value: Window = 11;
    assert_eq!(unsafe { std::mem::transmute::<Window, u16>(value) }, 11);
    assert_eq!(std::mem::size_of::<Window>(), 2);
    assert!(SPELLING.contains("pattern_type"));
}
