//@ compile-fail: error
trait Action {
    fn apply(&self) -> u32;
    fn sized_only(&self) -> u32 where Self: Sized;
}
impl Action for u32 {
    fn apply(&self) -> u32 { *self }
    fn sized_only(&self) -> u32 { *self + 1 }
}
fn main() {
    let method = <dyn Action as Action>::sized_only;
    let value: &dyn Action = &17u32;
    let _ = method(value);
}
