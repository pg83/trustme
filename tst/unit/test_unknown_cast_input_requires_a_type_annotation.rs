//@ compile-fail: type annotations needed
fn main() {
    let _ = |value| { let _ = value as u8; };
}
