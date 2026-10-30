struct Mark<'a>(&'a std::cell::Cell<u64>, u64);
impl Drop for Mark<'_> { fn drop(&mut self) { self.0.set(self.0.get() * 10 + self.1); } }
fn main() {
    let order = std::cell::Cell::new(0);
    {
        let (first, .., last): (Mark<'_>, u8, u16, Mark<'_>);
        last = Mark(&order, 4);
        first = Mark(&order, 1);
        assert_eq!((first.1, last.1), (1, 4));
    }
    assert_eq!(order.get(), 41);
}
