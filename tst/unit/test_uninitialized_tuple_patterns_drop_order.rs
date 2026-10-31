use std::cell::Cell;
struct Mark<'a>(u64, &'a Cell<u64>);
impl Drop for Mark<'_> {
    fn drop(&mut self) { self.1.set(self.1.get() * 10 + self.0); }
}
struct Triple<'a>(Mark<'a>, u16, Mark<'a>);
fn main() {
    let log = Cell::new(0);
    {
        let Triple(first, .., last): Triple<'_>;
        last = Mark(3, &log);
        first = Mark(1, &log);
        assert_eq!(first.0 + last.0, 4);
    }
    assert_eq!(log.replace(0), 31);
    {
        let (first, .., last): (Mark<'_>, bool, Mark<'_>);
        last = Mark(4, &log);
        first = Mark(2, &log);
        assert_eq!(first.0 + last.0, 6);
    }
    assert_eq!(log.get(), 42);
}
