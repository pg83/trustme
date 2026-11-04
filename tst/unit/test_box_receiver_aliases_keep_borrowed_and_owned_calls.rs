//@ compile-variant: -O
#![feature(arbitrary_self_types)]
type Handle<T> = Box<T>;
struct Leaf(u16);
impl Leaf {
    fn owned(self: Handle<Self>) -> u16 { self.0 }
    fn borrowed(self: &Handle<Self>) -> u16 { self.0 + 1 }
}
fn main() {
    let value = Box::new(Leaf(131));
    assert_eq!(value.borrowed(), 132);
    assert_eq!(Leaf::owned(value), 131);
}
