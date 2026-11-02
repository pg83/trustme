//@ compile-variant: -O
use std::cell::RefCell;

struct Mark<'a>(u8, &'a RefCell<Vec<u8>>);
impl Drop for Mark<'_> {
    fn drop(&mut self) { self.1.borrow_mut().push(self.0); }
}

fn replace<'a>(values: &mut [Mark<'a>], events: &'a RefCell<Vec<u8>>) {
    let [first, middle @ .., last] = values else { panic!("short input") };
    drop(std::mem::replace(first, Mark(11, events)));
    drop(std::mem::replace(&mut middle[1], Mark(13, events)));
    drop(std::mem::replace(last, Mark(15, events)));
    assert_eq!((first.0, middle[0].0, middle[1].0, last.0), (11, 2, 13, 15));
}

fn main() {
    let events = RefCell::new(Vec::new());
    {
        let mut values = [Mark(1, &events), Mark(2, &events), Mark(3, &events),
                          Mark(4, &events), Mark(5, &events)];
        replace(&mut values, &events);
        assert_eq!(*events.borrow(), [1, 3, 5]);
    }
    assert_eq!(*events.borrow(), [1, 3, 5, 11, 2, 13, 4, 15]);
}
