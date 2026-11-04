//@ compile-fail: #[unsafe(naked)] on non-function

#[unsafe(naked)]
struct Value(u32);

fn main() {}
