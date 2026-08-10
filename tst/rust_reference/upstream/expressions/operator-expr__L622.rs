// Extracted from src/expressions/operator-expr.md:622
#![allow(unused)]
fn main() {
    assert_eq!(1_234.5f32 as f64, 1_234.5f64);
      assert_eq!(std::f32::INFINITY as f64, std::f64::INFINITY);
      assert!((std::f32::NAN as f64).is_nan());
}
