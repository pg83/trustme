const LAST: u8 = 9;
struct Limits;
impl Limits { const LOW: u8 = 2; const HIGH: u8 = 6; }
macro_rules! accepts {
    ($p:pat, $value:expr) => { matches!($value, $p) };
}
macro_rules! below {
    ($end:path, $value:expr) => { accepts!(| ..=$end, $value) };
}
fn main() {
    assert!(accepts!(| ..=LAST, 9));
    assert!(!accepts!(..LAST, 9));
    assert!(accepts!(Limits::LOW..=Limits::HIGH, 5));
    assert!(!accepts!(Limits::LOW..Limits::HIGH, 6));
    assert!(below!(Limits::HIGH, 6));
    assert!(!below!(LAST, 10));
    assert!(accepts!(| ..=::std::primitive::u8::MAX, 255u8));
    assert!(accepts!(1..=::std::primitive::u8::MAX, 254u8));
    assert!(accepts!(b'a'..=b'f', b'e'));
    assert!(!accepts!(b'a'..b'f', b'f'));
}
