use std::cell::Cell;
struct Mark<'a>(u64, &'a Cell<u64>);
impl Drop for Mark<'_> {
    fn drop(&mut self) { self.1.set(self.1.get() * 10 + self.0); }
}
fn main() {
    let log = Cell::new(0);
    {
        let [first, middle @ .., last]: [Mark<'_>; 4];
        last = Mark(4, &log);
        first = Mark(1, &log);
        middle = [Mark(2, &log), Mark(3, &log)];
        assert_eq!(first.0 + middle[0].0 + middle[1].0 + last.0, 10);
    }
    assert_eq!(log.replace(0), 4231);
    {
        let [first, last]: [Mark<'_>; 2];
        last = Mark(6, &log);
        first = Mark(5, &log);
        assert_eq!(first.0 + last.0, 11);
    }
    assert_eq!(log.get(), 65);
}
