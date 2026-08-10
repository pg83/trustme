//@ check-pass

#![feature(type_alias_impl_trait)]

trait SuperExpectation: Fn(i32) {}

impl<T: Fn(i32)> SuperExpectation for T {}

type Closure = impl SuperExpectation;

#[define_opaque(Closure)]
fn define_closure() {
    let _: Closure = |value| {
        let _ = value.to_string();
    };
}

fn main() {
    define_closure();
}
