//@ compile-fail: No applicable methods
use std::rc::Rc;
struct Unclonable;
struct Parcel<T>(T);
impl<T: Clone> Parcel<T> {
    fn inspect(self: Rc<Self>) -> u32 { 97 }
}
fn main() {
    let _ = Rc::new(Parcel(Unclonable)).inspect();
}
