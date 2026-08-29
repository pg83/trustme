//@ check-pass

use std::pin::Pin;

trait Action {
    fn run(self: Pin<&mut Self>);
}

impl<T: ?Sized + Action + Unpin> Action for &mut T {
    fn run(mut self: Pin<&mut Self>) {
        T::run(Pin::new(&mut *self));
    }
}

fn main() {}
