//@ compile-fail: Multiple applicable inherent functions
struct Choice<T>(T);
impl Choice<u8> { fn make() -> Self { Choice(13) } }
impl Choice<u16> { fn make() -> Self { Choice(37) } }
fn main() { let _: Choice<u8> = Choice::<_>::make(); }
