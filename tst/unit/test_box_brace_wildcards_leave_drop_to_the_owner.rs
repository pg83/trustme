use std::cell::Cell;
struct Watch<'a>(&'a Cell<u16>);
impl Drop for Watch<'_> { fn drop(&mut self) { self.0.set(self.0.get() + 1); } }
fn main() {
    let dropped = Cell::new(0);
    let value = Box::new(Watch(&dropped));
    match value { Box { .. } => assert_eq!(dropped.get(), 0) }
    assert_eq!(dropped.get(), 0);
    drop(value);
    assert_eq!(dropped.get(), 1);
}
