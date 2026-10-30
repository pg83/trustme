use std::cell::RefCell;
use std::rc::Rc;
struct Tracked(u8, Rc<RefCell<Vec<u8>>>);
impl Drop for Tracked { fn drop(&mut self) { self.1.borrow_mut().push(100 + self.0); } }
struct Pair(Tracked, Tracked);
fn make(log: &Rc<RefCell<Vec<u8>>>, index: u8) -> Tracked { log.borrow_mut().push(index); Tracked(index, log.clone()) }
fn fail(log: &Rc<RefCell<Vec<u8>>>) -> Tracked { log.borrow_mut().push(9); panic!("stop construction"); }
fn main() {
    let log = Rc::new(RefCell::new(Vec::new()));
    let pair = Pair { 1: make(&log, 1), 0: make(&log, 0) };
    assert_eq!((pair.0.0, pair.1.0), (0, 1));
    assert_eq!(&*log.borrow(), &[1, 0]);
    drop(pair);
    assert_eq!(&*log.borrow(), &[1, 0, 100, 101]);
    log.borrow_mut().clear();
    let failed = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        let _pair = Pair { 1: make(&log, 1), 0: fail(&log) };
    }));
    assert!(failed.is_err());
    assert_eq!(&*log.borrow(), &[1, 9, 101]);
}
