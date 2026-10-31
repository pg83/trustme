use std::cell::Cell;
struct Mark<'a>(u64, &'a Cell<u64>);
impl Drop for Mark<'_> {
    fn drop(&mut self) { self.1.set(self.1.get() * 10 + self.0); }
}
struct Pair<'a> { first: Mark<'a>, second: Mark<'a> }
fn main() {
    let log = Cell::new(0);
    {
        let Pair { second, first }: Pair<'_>;
        first = Mark(1, &log);
        second = Mark(2, &log);
        assert_eq!(first.0 + second.0, 3);
        assert_eq!(log.get(), 0);
    }
    assert_eq!(log.get(), 12);
}
