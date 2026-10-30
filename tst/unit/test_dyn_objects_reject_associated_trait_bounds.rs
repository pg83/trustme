//@ compile-fail: Associated type trait bounds not allowed here
trait Read { type Output; }
fn rejected(_: &dyn Read<Output: Copy>) {}
fn main() {}
