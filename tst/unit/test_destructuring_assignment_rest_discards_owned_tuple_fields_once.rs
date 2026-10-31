use std::cell::Cell;
struct Mark<'a>(&'a Cell<u32>, u32);
impl Drop for Mark<'_> { fn drop(&mut self) { self.0.set(self.0.get() * 10 + self.1); } }
fn main() {
    let log = Cell::new(0);
    _ = Mark(&log, 1);
    (..) = (Mark(&log, 2), Mark(&log, 3));
    assert_eq!(log.get(), 123);
}
