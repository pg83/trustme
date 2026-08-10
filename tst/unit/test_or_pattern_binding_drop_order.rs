#![allow(irrefutable_let_patterns, unused_assignments, unused_variables)]

use std::cell::RefCell;

struct DropOrder(RefCell<Vec<u32>>);

struct LogDrop<'a>(&'a DropOrder, u32);

impl<'a> Drop for LogDrop<'a> {
    fn drop(&mut self) {
        self.0.0.borrow_mut().push(self.1);
    }
}

fn assert_drop_order(expected: &[u32], f: impl FnOnce(&DropOrder)) {
    let order = DropOrder(RefCell::new(Vec::new()));
    f(&order);
    assert_eq!(&*order.0.borrow(), expected);
}

fn main() {
    assert_drop_order(&[11, 12, 13], |order| {
        let (x, Ok(y) | Err(y), z);
        z = LogDrop(order, 11);
        y = LogDrop(order, 12);
        x = LogDrop(order, 13);
    });

    assert_drop_order(&[21, 22, 23], |order| {
        let (x, Ok(y) | Err(y), z) =
            (LogDrop(order, 23), Ok(LogDrop(order, 21)), LogDrop(order, 22));
    });

    assert_drop_order(&[31, 32], |order| {
        let ((true, x, y) | (false, y, x)) =
            (false, LogDrop(order, 31), LogDrop(order, 32));
    });

    assert_drop_order(&[41, 42], |order| {
        match (true, LogDrop(order, 41), LogDrop(order, 42)) {
            (true, x, y) | (false, y, x) => {}
        }
    });

    assert_drop_order(&[51, 52, 53], |order| {
        (|(x, Ok(y) | Err(y), z)| {})((
            LogDrop(order, 53),
            Ok(LogDrop(order, 51)),
            LogDrop(order, 52),
        ));
    });

    assert_drop_order(&[61, 62, 63], |order| {
        if let (x, Ok(y) | Err(y), z) =
            (LogDrop(order, 63), Ok(LogDrop(order, 61)), LogDrop(order, 62))
        {}
    });

    assert_drop_order(&[71, 72, 73], |order| {
        (|x, (Ok(y) | Err(y)), z| {})(
            LogDrop(order, 73),
            Ok(LogDrop(order, 72)),
            LogDrop(order, 71),
        );
    });

    assert_drop_order(&[81, 82, 83, 84], |order| {
        let (a, ref b) = (LogDrop(order, 83), LogDrop(order, 84));
        let (ref c, d) = (LogDrop(order, 82), LogDrop(order, 81));
        let _ = (&a, b, c, &d);
    });

    assert_drop_order(&[91, 92, 93], |order| {
        let (x, Ok(y) | Err(y), z) =
            (LogDrop(order, 93), Ok(LogDrop(order, 91)), LogDrop(order, 92))
        else {
            panic!();
        };
    });
}
