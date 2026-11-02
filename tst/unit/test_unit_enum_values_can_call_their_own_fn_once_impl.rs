//@ compile-variant: -O
#![feature(fn_traits, unboxed_closures)]
enum Action { Increment, Double }
impl FnOnce<(u16,)> for Action {
    type Output = u16;
    extern "rust-call" fn call_once(self, (value,): (u16,)) -> u16 {
        match self { Action::Increment => value + 1, Action::Double => value * 2 }
    }
}
fn main() {
    assert_eq!(Action::Increment(11), 12);
    assert_eq!(Action::Double(11), 22);
    let action = Action::Increment;
    assert_eq!(action(23), 24);
}
