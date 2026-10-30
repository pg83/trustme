//@ aux-build: numeric_literal_shapes.rs
use std::cell::RefCell;
use numeric_literal_shapes::{Choice, Pair};
type Alias = Pair;
fn field(log: &RefCell<Vec<u8>>, index: u8, value: u16) -> u16 { log.borrow_mut().push(index); value }
fn main() {
    let log = RefCell::new(Vec::new());
    let pair = Alias { 1: field(&log, 1, 13), 0: field(&log, 0, 7) };
    assert_eq!(pair, Pair(7, 13));
    assert_eq!(&*log.borrow(), &[1, 0]);
    log.borrow_mut().clear();
    let choice = Choice::Pair { 1: field(&log, 3, 23), 0: field(&log, 2, 19) };
    assert_eq!(choice, Choice::Pair(19, 23));
    assert_eq!(&*log.borrow(), &[3, 2]);
}
