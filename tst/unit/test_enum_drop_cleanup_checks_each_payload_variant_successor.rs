use std::cell::Cell;

struct Mark<'a>(&'a Cell<u32>, u32);
impl Drop for Mark<'_> {
    fn drop(&mut self) { self.0.set(self.0.get() + self.1); }
}
enum Envelope<'a> {
    Empty,
    One(Mark<'a>),
    Pair(Mark<'a>, Mark<'a>),
}
impl Drop for Envelope<'_> {
    fn drop(&mut self) {
        if std::hint::black_box(false) { panic!("envelope drop failed"); }
    }
}
fn main() {
    let count = Cell::new(0);
    drop(Envelope::Empty);
    drop(Envelope::One(Mark(&count, 1)));
    drop(Envelope::Pair(Mark(&count, 2), Mark(&count, 4)));
    assert_eq!(count.get(), 7);
}
