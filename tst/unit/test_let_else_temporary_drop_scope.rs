#![allow(irrefutable_let_patterns)]

use std::cell::RefCell;

struct Events(RefCell<Vec<&'static str>>);

struct LogDrop<'a>(&'a Events, &'static str);

impl<'a> Drop for LogDrop<'a> {
    fn drop(&mut self) {
        self.0.0.borrow_mut().push(self.1);
    }
}

enum MaybeDrop<'a> {
    Some(LogDrop<'a>),
    None(LogDrop<'a>),
}

fn main() {
    let success = Events(RefCell::new(Vec::new()));
    loop {
        let &_ = &LogDrop(&success, "success drop") else {
            panic!();
        };
        success.0.borrow_mut().push("body");
        break;
    }
    assert_eq!(&*success.0.borrow(), &["body", "success drop"]);

    let failure = Events(RefCell::new(Vec::new()));
    loop {
        let MaybeDrop::Some(_) = MaybeDrop::None(LogDrop(&failure, "value drop")) else {
            failure.0.borrow_mut().push("else");
            break;
        };
        panic!();
    }
    assert_eq!(&*failure.0.borrow(), &["value drop", "else"]);

    let borrowed_failure = Events(RefCell::new(Vec::new()));
    loop {
        let &MaybeDrop::Some(_) = &MaybeDrop::None(LogDrop(&borrowed_failure, "borrowed drop")) else {
            borrowed_failure.0.borrow_mut().push("else");
            break;
        };
        panic!();
    }
    assert_eq!(&*borrowed_failure.0.borrow(), &["borrowed drop", "else"]);
}
