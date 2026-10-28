//@ compile-fail: is already used for a generic parameter
struct Value<T>(T);
impl<T> Value<T> {
    fn duplicate<T>() {}
}
fn main() {}
