// The type of a `*place = value` assignment's left side comes from the deref,
// which cannot run until the method producing the pointer resolves. Until then
// it is an inference variable, and the finalisation rule that lets a coercion
// destination with nothing else to go on take its source type claimed it - so
// the place became the reference being stored rather than the pointer it holds.
// A variable that a node still waiting to be revisited will produce is not
// unconstrained: that node is what constrains it.

struct Holder<T>(*mut T);

impl<T> Holder<T> {
    fn new(pointer: *mut T) -> Self {
        Holder(pointer)
    }

    fn get_mut(&mut self) -> &mut *mut T {
        &mut self.0
    }
}

fn main() {
    let mut first = 10i32;
    let mut holder = Holder::new(&mut first);
    let mut second: i32 = 5;
    *holder.get_mut() = &mut second;
    assert_eq!(unsafe { *holder.0 }, 5);
}
