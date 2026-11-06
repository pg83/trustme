//@ compile-fail: error:

trait Project {
    type Output;
}

impl Project for () {
    type Output = ();
}

fn project<T: Project>(_: T) -> T::Output { loop {} }

fn main() {
    let mut value = Default::default();
    value = project(value);
    let _ = value;
}
