#![feature(arbitrary_self_types)]

#[derive(Clone)]
struct Receiver<'a, T: ?Sized>(&'a T);

impl<'a, T: ?Sized> std::ops::Receiver for Receiver<'a, T> {
    type Target = T;
}

#[derive(Clone)]
struct Target;

impl Target {
    fn method<'a>(self: Receiver<'a, Self>) -> usize {
        1
    }
}

fn main() {
    let target = Target;
    let receiver = Receiver(&target);
    assert_eq!(receiver.method(), 1);
}
