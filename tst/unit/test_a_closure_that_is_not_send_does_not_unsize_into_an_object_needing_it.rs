//@ compile-fail: Failed to find an impl of ::"core-0_0_0"::marker::Send
// The obligation a coercion into a trait object keeps is still proved once the
// closure's captures are known: a closure holding an `Rc` is not `Send`
// (rustc: `Rc<u32>` cannot be sent between threads safely).
use std::rc::Rc;

trait Tr {
    fn go(&mut self, x: &mut u32);
}

impl<F: FnMut(&mut u32) + Send> Tr for F {
    fn go(&mut self, x: &mut u32) {
        self(x)
    }
}

fn main() {
    let shared = Rc::new(1u32);
    let b: Box<dyn Tr> = Box::new(move |s: &mut u32| *s += *shared);
    drop(b);
}
