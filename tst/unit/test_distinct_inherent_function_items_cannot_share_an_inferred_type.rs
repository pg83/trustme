//@ compile-fail: mismatch
struct Operations;
impl Operations {
    fn first(value: u16) -> u16 { value + 1 }
    fn second(value: u16) -> u16 { value + 2 }
}
fn require_same<T>(_: T, _: T) {}

fn main() {
    require_same(Operations::first, Operations::second);
}
