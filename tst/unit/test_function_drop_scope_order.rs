use std::cell::RefCell;

struct DropOrder(RefCell<Vec<u32>>);

struct LogDrop<'a>(&'a DropOrder, u32);

impl<'a> LogDrop<'a> {
    fn next(&self, value: u32) -> LogDrop<'a> {
        LogDrop(self.0, value)
    }
}

impl<'a> Drop for LogDrop<'a> {
    fn drop(&mut self) {
        self.0.0.borrow_mut().push(self.1);
    }
}

fn nested_temporary_order<'a>(arg0: LogDrop<'a>, arg1: LogDrop<'a>) -> LogDrop<'a> {
    let local2 = arg1.next(2);
    let local5 = {
        let local3 = arg0.next(3);
        local2.next(4).next(5)
    };
    local5.next(6).next(7)
}

fn main() {
    let order = DropOrder(RefCell::new(Vec::new()));
    let result = nested_temporary_order(LogDrop(&order, 0), LogDrop(&order, 1));
    drop(result);
    assert_eq!(&*order.0.borrow(), &[3, 4, 5, 2, 6, 1, 0, 7]);
}
