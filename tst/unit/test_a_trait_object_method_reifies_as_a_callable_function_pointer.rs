//@ compile-fail: error
trait Action { fn apply(&self, delta: u32) -> u32; }
struct Accumulator(u32);
impl Action for Accumulator {
    fn apply(&self, delta: u32) -> u32 { self.0 + delta }
}
fn main() {
    let apply: fn(&dyn Action, u32) -> u32 = <dyn Action as Action>::apply;
    let value = Accumulator(17);
    assert_eq!(apply(&value, 23), 40);
}
