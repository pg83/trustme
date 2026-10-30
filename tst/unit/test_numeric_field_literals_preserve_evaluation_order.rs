use std::cell::RefCell;
#[derive(Debug, PartialEq)]
struct Pair<T>(T, T);
type Bytes = Pair<u16>;
#[derive(Debug, PartialEq)]
enum Choice { Pair(u16, u16) }
struct Mixed(u8, &'static str);
struct Named { left: u16, right: u16 }
fn field(log: &RefCell<Vec<u8>>, index: u8, value: u16) -> u16 { log.borrow_mut().push(index); value }
fn trace(log: &RefCell<Vec<u8>>, expected: &[u8]) { assert_eq!(&*log.borrow(), expected); log.borrow_mut().clear(); }
impl Pair<u16> {
    fn make(log: &RefCell<Vec<u8>>) -> Self { Self { 1: field(log, 1, 90), 0: field(log, 0, 40) } }
}
fn main() {
    let log = RefCell::new(Vec::new());
    assert_eq!(Pair::make(&log), Pair(40, 90));
    trace(&log, &[1, 0]);
    let alias = Bytes { 1: field(&log, 3, 17), 0: field(&log, 2, 11) };
    assert_eq!(alias, Pair(11, 17));
    trace(&log, &[3, 2]);
    let pair = Pair::<u16> { 1: field(&log, 5, 29), 0: field(&log, 4, 23) };
    assert_eq!(pair, Pair(23, 29));
    trace(&log, &[5, 4]);
    let choice = Choice::Pair { 1: field(&log, 7, 41), 0: field(&log, 6, 37) };
    assert_eq!(choice, Choice::Pair(37, 41));
    trace(&log, &[7, 6]);
    let mut value = 11u16;
    let pair = Pair { 1: value, 0: { value = 19; value } };
    assert_eq!(pair, Pair(19, 11));
    assert_eq!(value, 19);
    let base = Pair(43u16, 47);
    let pair = Pair { 1: field(&log, 9, 53), ..{ log.borrow_mut().push(8); base } };
    assert_eq!(pair, Pair(43, 53));
    trace(&log, &[9, 8]);
    let mixed = Mixed { 1: "tail", 0: 7 };
    assert_eq!((mixed.0, mixed.1), (7, "tail"));
    let named = Named { right: field(&log, 11, 61), left: field(&log, 10, 59) };
    assert_eq!((named.left, named.right), (59, 61));
    trace(&log, &[11, 10]);
}
