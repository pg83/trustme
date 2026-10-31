use std::cell::Cell;
union Counter<'a> { log: &'a Cell<u32>, unused: usize }
impl Drop for Counter<'_> { fn drop(&mut self) { unsafe { self.log.set(self.log.get() + 1); } } }
fn main() {
    let log = Cell::new(0);
    { let _value = Counter { log: &log }; }
    assert_eq!(log.get(), 1);
}
