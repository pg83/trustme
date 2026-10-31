struct Flags(u8);
struct Signed(i16);
impl std::ops::Not for Flags {
    type Output = u8;
    fn not(self) -> u8 { self.0 ^ 0x0f }
}
impl std::ops::Neg for Signed {
    type Output = i32;
    fn neg(self) -> i32 { -(self.0 as i32) - 1 }
}
fn main() {
    let inverted: u8 = !Flags(0x35);
    let negated: i32 = -Signed(-37);
    assert_eq!(inverted, 0x3a);
    assert_eq!(negated, 36);
}
