// Extracted from library/alloc/src/rc.rs:2840
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::rc::Rc;
    use std::borrow::Cow;
    let cow: Cow<'_, str> = Cow::Borrowed("eggplant");
    let shared: Rc<str> = Rc::from(cow);
    assert_eq!("eggplant", &shared[..]);
}
