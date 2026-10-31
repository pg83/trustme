use std::cell::Cell;
use std::panic::{catch_unwind, AssertUnwindSafe};
struct Leaf<'a>(&'a Cell<u32>, u32);
impl Drop for Leaf<'_> { fn drop(&mut self) { self.0.set(self.0.get() * 10 + self.1); } }
enum Choice<'a> { Present(Leaf<'a>, Leaf<'a>), Empty }
impl Drop for Choice<'_> { fn drop(&mut self) { panic!("choice destructor"); } }
fn main() {
    let log = Cell::new(0);
    assert!(catch_unwind(AssertUnwindSafe(|| drop(Choice::Present(Leaf(&log, 1), Leaf(&log, 2))))).is_err());
    assert_eq!(log.get(), 12);
}
