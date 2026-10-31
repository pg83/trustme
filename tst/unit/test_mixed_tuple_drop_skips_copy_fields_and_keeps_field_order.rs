use std::cell::Cell;
struct Leaf<'a>(&'a Cell<u32>, u32);
impl Drop for Leaf<'_> { fn drop(&mut self) { self.0.set(self.0.get() * 10 + self.1); } }
fn marker() {}
fn main() {
    let log = Cell::new(0);
    drop((Leaf(&log, 1), 17u32, Leaf(&log, 2), marker as fn()));
    assert_eq!(log.get(), 12);
}
