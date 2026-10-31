//@ compile-fail: Type mismatch
fn make() -> impl Fn(u8) -> u16 {
    |value| [value; 2]
}

fn main() {}
