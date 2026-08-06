// Extracted from src/expressions/operator-expr.md:569
#![allow(unused)]
fn main() {
    assert_eq!(42u16 as u8, 42u8);
      assert_eq!(1234u16 as u8, 210u8);
      assert_eq!(0xabcdu16 as u8, 0xcdu8);
    
      assert_eq!(-42i16 as i8, -42i8);
      assert_eq!(1234u16 as i8, -46i8);
      assert_eq!(0xabcdi32 as i8, -51i8);
}
