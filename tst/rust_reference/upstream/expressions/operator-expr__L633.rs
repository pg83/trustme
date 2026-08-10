// Extracted from src/expressions/operator-expr.md:633
#![allow(unused)]
fn main() {
    assert_eq!(1_234.5f64 as f32, 1_234.5f32);
      assert_eq!(1_234_567_891.123f64 as f32, 1_234_567_890f32, "Rounded");
      assert_eq!(std::f64::INFINITY as f32, std::f32::INFINITY);
      assert!((std::f64::NAN as f32).is_nan());
}
