//@ compile-fail: error
struct NotClone;
fn clone_it<T: Clone>(value: T) -> T {
    value.clone()
}
fn main() {
    clone_it(NotClone);
}
