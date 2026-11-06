//@ compile-fail: cannot be used through a trait object
trait Dynamic {
    fn generic<T>(&self);
}
fn main() {
    let pointer: fn(&(dyn Dynamic + 'static)) = <dyn Dynamic as Dynamic>::generic::<u8>;
    std::hint::black_box(pointer);
}
