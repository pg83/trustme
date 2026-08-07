// Extracted from library/alloc/src/rc.rs:1365
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::rc::Rc;

    let five = Rc::new(5);

    unsafe {
        let ptr = Rc::into_raw(five);
        Rc::increment_strong_count(ptr);

        let five = Rc::from_raw(ptr);
        assert_eq!(2, Rc::strong_count(&five));
      // Prevent leaks for Miri.
      Rc::decrement_strong_count(ptr);
    }
}
