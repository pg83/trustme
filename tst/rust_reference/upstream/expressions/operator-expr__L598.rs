// Extracted from src/expressions/operator-expr.md:598
#![allow(unused)]
fn main() {
    assert_eq!(42.9f32 as i32, 42);
      assert_eq!(-42.9f32 as i32, -42);
      assert_eq!(42_000_000f32 as i32, 42_000_000);
      assert_eq!(std::f32::NAN as i32, 0);
      assert_eq!(1_000_000_000_000_000f32 as i32, 0x7fffffffi32);
      assert_eq!(std::f32::NEG_INFINITY as i32, -0x80000000i32);
}
