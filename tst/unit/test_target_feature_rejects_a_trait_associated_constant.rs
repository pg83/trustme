//@ compile-fail: #[target_feature] should be applied to a function definition
trait Traits { #[target_feature(enable = "sse2")] const VALUE: u8; }
fn main() {}
