// Extracted from src/expressions/operator-expr.md:584
#![allow(unused)]
fn main() {
    assert_eq!(42i8 as i16, 42i16);
      assert_eq!(-17i8 as i16, -17i16);
      assert_eq!(0b1000_1010u8 as u16, 0b0000_0000_1000_1010u16, "Zero-extend");
      assert_eq!(0b0000_1010i8 as i16, 0b0000_0000_0000_1010i16, "Sign-extend 0");
      assert_eq!(0b1000_1010u8 as i8 as i16, 0b1111_1111_1000_1010u16 as i16, "Sign-extend 1");
}
