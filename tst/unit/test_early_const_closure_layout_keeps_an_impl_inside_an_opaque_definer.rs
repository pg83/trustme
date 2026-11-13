#![feature(type_alias_impl_trait)]
//@ compile-fail: type annotations needed
type Operation = impl Fn(u8) -> u8 + Copy;

#[define_opaque(Operation)]
const fn create() -> Operation {
    |value| value + 3
}

struct Ready([u8; core::mem::size_of_val(&create())]);

trait Route {}

impl<T> Route for (T, Operation) {}

fn retain<T: Route>(value: T) -> T {
    value
}

#[define_opaque(Operation)]
fn relay(value: Operation) -> Operation {
    retain(((), value)).1
}

fn main() {
    let Ready(bytes) = Ready([]);
    assert_eq!(bytes.len(), 0);
    let operation = relay(create());
    let copy = operation;
    assert_eq!(operation(17), 20);
    assert_eq!(copy(23), 26);
}
