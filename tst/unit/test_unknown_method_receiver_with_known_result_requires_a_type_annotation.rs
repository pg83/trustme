//@ compile-fail: type annotations needed
struct Value;
impl Value { fn byte(&self) -> u8 { 7 } }
fn main() {
    let _ = |receiver| { let _: u8 = receiver.byte(); };
}
