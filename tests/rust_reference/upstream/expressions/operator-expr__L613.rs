// Extracted from src/expressions/operator-expr.md:613
#![allow(unused)]
fn main() {
    assert_eq!(1337i32 as f32, 1337f32);
      assert_eq!(123_456_789i32 as f32, 123_456_790f32, "Rounded");
      assert_eq!(0xffffffff_ffffffff_ffffffff_ffffffff_u128 as f32, std::f32::INFINITY);
}
