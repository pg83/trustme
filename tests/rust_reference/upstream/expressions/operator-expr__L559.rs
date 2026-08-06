// Extracted from src/expressions/operator-expr.md:559
#![allow(unused)]
fn main() {
    assert_eq!(42i8 as u8, 42u8);
      assert_eq!(-1i8 as u8, 255u8);
      assert_eq!(255u8 as i8, -1i8);
      assert_eq!(-1i16 as u16, 65535u16);
}
