//@ compile-fail: Project
//@ compile-flags: -Cpanic=abort

trait Project<F> {
    type Value;
}

fn project<F, T: Project<F>>(_: F, _: T) -> T::Value {
    loop {}
}

fn main() {
    let identity = |value| value;
    identity(project(identity, ()));
}
