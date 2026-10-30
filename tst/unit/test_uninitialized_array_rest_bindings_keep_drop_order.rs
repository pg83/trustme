use std::cell::RefCell;
use std::rc::Rc;
struct Tracked(u8, Rc<RefCell<Vec<u8>>>);
impl Drop for Tracked { fn drop(&mut self) { self.1.borrow_mut().push(self.0); } }
fn main() {
    let log = Rc::new(RefCell::new(Vec::new()));
    {
        let [first, middle @ .., last]: [Tracked; 4];
        last = Tracked(4, log.clone());
        first = Tracked(1, log.clone());
        middle = [Tracked(2, log.clone()), Tracked(3, log.clone())];
        assert_eq!((first.0, middle[0].0, middle[1].0, last.0), (1, 2, 3, 4));
    }
    assert_eq!(&*log.borrow(), &[4, 2, 3, 1]);
    log.borrow_mut().clear();
    {
        let [first, .., last]: [Tracked; 2];
        first = Tracked(5, log.clone());
        last = Tracked(6, log.clone());
        assert_eq!((first.0, last.0), (5, 6));
    }
    assert_eq!(&*log.borrow(), &[6, 5]);
}
