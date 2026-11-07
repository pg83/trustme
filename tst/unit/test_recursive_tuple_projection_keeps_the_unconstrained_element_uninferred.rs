//@ compile-fail: error:

trait Project { type Value; }
impl<T> Project for (T,) { type Value = T; }
impl<T> Project for [T; 1] { type Value = T; }

fn project<T: Project>(_: T) -> T::Value {
    loop {}
}

fn main() {
    let mut value = Default::default();
    value = (project(value),);
    let _ = value;
}
