use std::cell::Cell;
struct Mark<'a>(u64, &'a Cell<u64>);
impl Drop for Mark<'_> {
    fn drop(&mut self) { self.1.set(self.1.get() * 10 + self.0); }
}
fn main() {
    let log = Cell::new(0);
    {
        let ((value, _) | (_, value)): (Mark<'_>, Mark<'_>);
        value = Mark(8, &log);
        assert_eq!(value.0, 8);
        assert_eq!(log.get(), 0);
    }
    assert_eq!(log.get(), 8);
}
