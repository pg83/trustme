//@ compile-fail: Multiple applicable inherent functions
struct Choice<T>(T);
impl Choice<u8> { fn choose(value: u8) -> Self { Choice(value) } }
impl Choice<u16> { fn choose(value: u16) -> Self { Choice(value) } }
fn main() { let _: Choice<u8> = Choice::<_>::choose(37_u8); }
