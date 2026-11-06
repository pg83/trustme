//@ compile-fail: cannot be used through a trait object
trait Dynamic {
    fn generic<T>(&self);
}
fn main() {
    let _ = <dyn Dynamic as Dynamic>::generic::<u8>;
}
