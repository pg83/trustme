//@ compile-variant: -O
use std::cell::RefCell;

struct Mark<'a>(u8, &'a RefCell<Vec<u8>>);
impl Drop for Mark<'_> {
    fn drop(&mut self) { self.1.borrow_mut().push(self.0); }
}
struct Nested<T> { values: Box<[T; 3]> }

fn move_parts<'a>(value: Nested<Mark<'a>>, split: bool) {
    if split {
        let [first, rest @ ..] = *value.values;
        drop(rest);
        drop(first);
    } else {
        let [first, middle, last] = *value.values;
        drop(middle);
        drop(first);
        drop(last);
    }
}

fn main() {
    for split in [false, true] {
        let events = RefCell::new(Vec::new());
        let values = Box::new([Mark(1, &events), Mark(2, &events), Mark(3, &events)]);
        move_parts(Nested { values }, std::hint::black_box(split));
        assert_eq!(*events.borrow(), if split { vec![2, 3, 1] } else { vec![2, 1, 3] });
    }
}
