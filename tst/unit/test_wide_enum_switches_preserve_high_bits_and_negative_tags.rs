#[repr(i128)]
enum Signed { Bottom = i128::MIN, Negative = -(1i128 << 100), Positive = 1i128 << 100, Top = i128::MAX }
#[repr(u128)]
enum Unsigned { Low = 3, Middle = 1u128 << 100, High = u128::MAX }
#[inline(never)]
fn signed(value: Signed) -> u8 {
    match value { Signed::Bottom => 3, Signed::Negative => 5, Signed::Positive => 7, Signed::Top => 11 }
}
#[inline(never)]
fn unsigned(value: Unsigned) -> u8 {
    match value { Unsigned::Low => 13, Unsigned::Middle => 17, Unsigned::High => 19 }
}
fn main() {
    assert_eq!((signed(Signed::Bottom), signed(Signed::Negative), signed(Signed::Positive), signed(Signed::Top)), (3, 5, 7, 11));
    assert_eq!((unsigned(Unsigned::Low), unsigned(Unsigned::Middle), unsigned(Unsigned::High)), (13, 17, 19));
}
