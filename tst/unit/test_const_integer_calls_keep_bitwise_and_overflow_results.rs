const fn unsigned(left: u16, right: u16) -> u16 { left | right }
const fn signed(left: i16, right: i16) -> (i16, i16, bool, bool, bool) {
    (left & right, left ^ right, left != right, left >= right, left <= right)
}
const UNSIGNED: u16 = unsigned(0x1200, 0x34);
const SIGNED: (i16, i16, bool, bool, bool) = signed(-17, 37);
const PRODUCT: (u16, bool) = 65535u16.overflowing_mul(17);
const SIGNED_PRODUCT: (i16, bool) = (-32768i16).overflowing_mul(-1);
fn main() {
    assert_eq!(UNSIGNED, 0x1234);
    assert_eq!(SIGNED, (37, -54, true, false, true));
    assert_eq!(PRODUCT, (65519, true));
    assert_eq!(SIGNED_PRODUCT, (-32768, true));
}
