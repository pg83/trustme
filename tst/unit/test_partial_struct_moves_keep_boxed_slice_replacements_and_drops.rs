//@ compile-variant: -O
use std::cell::RefCell;
struct Mark<'a>(u32, &'a RefCell<Vec<u32>>);
impl Drop for Mark<'_> { fn drop(&mut self) { self.1.borrow_mut().push(self.0); } }
struct Holder<'a> { name: Mark<'a>, values: Box<[Mark<'a>]> }
fn main() {
    for early in [false, true] {
        let log = RefCell::new(Vec::new());
        {
            let mut holder = Holder {
                name: Mark(1, &log),
                values: Box::new([Mark(2, &log), Mark(3, &log), Mark(4, &log)]),
            };
            if std::hint::black_box(early) { drop(holder.name); }
            holder.values[0] = Mark(12, &log);
            drop(std::mem::replace(&mut holder.values[2], Mark(14, &log)));
            assert_eq!((holder.values[0].0, holder.values[1].0, holder.values[2].0), (12, 3, 14));
        }
        assert_eq!(*log.borrow(), if early { vec![1, 2, 4, 12, 3, 14] } else { vec![2, 4, 1, 12, 3, 14] });
    }
}
