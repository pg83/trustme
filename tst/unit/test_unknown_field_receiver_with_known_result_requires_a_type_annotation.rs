//@ compile-fail: type annotations needed
struct Value { byte: u8 }
fn main() {
    let _ = |receiver| { let _: u8 = receiver.byte; };
}
