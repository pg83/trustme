//@ compile-variant: -O
struct Flag(bool);
impl std::ops::Not for Flag { type Output = bool; fn not(self) -> bool { !self.0 } }
struct Signed(i32);
impl std::ops::Neg for Signed { type Output = i32; fn neg(self) -> i32 { -self.0 } }
#[inline(never)]
fn transform(mut value: u16, shift: u8) -> u16 {
    value <<= shift;
    value ^= 3;
    value |= 16;
    value &= 31;
    value >>= 1u32;
    value += 2;
    value
}
fn main() {
    assert_eq!(transform(5, 1), 14);
    assert_eq!(transform(9, 0), 15);
    assert!(!Flag(false));
    assert!(!(!Flag(true)));
    assert_eq!(-Signed(17), -17);
    assert_eq!(-Signed(-19), 19);
}
