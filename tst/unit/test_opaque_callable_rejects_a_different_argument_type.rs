//@ compile-fail: Failed to find an impl
fn make() -> impl Fn(u8) -> u16 {
    |value: u16| value
}

fn main() {}
