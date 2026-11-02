//@ compile-variant: -O
use std::cell::RefCell;
struct Mark<'a>(u32, &'a RefCell<Vec<u32>>);
impl Drop for Mark<'_> { fn drop(&mut self) { self.1.borrow_mut().push(self.0); } }
fn main() {
    let log = RefCell::new(Vec::new());
    {
        let mut values: Box<[Mark<'_>]> = Box::new([Mark(1, &log), Mark(2, &log), Mark(3, &log), Mark(4, &log)]);
        let [ref mut first, ref mut middle @ .., ref mut last] = *values else { panic!("short") };
        first.0 += 10;
        middle[1].0 += 20;
        last.0 += 30;
        assert_eq!((first.0, middle[0].0, middle[1].0, last.0), (11, 2, 23, 34));
    }
    assert_eq!(*log.borrow(), [11, 2, 23, 34]);
}
