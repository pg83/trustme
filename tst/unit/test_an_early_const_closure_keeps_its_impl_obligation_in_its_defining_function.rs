#![feature(type_alias_impl_trait)]

type Operation = impl Fn(u8) -> u8 + Copy;

trait Route {}

impl<T> Route for (T, Operation) {}

fn retain<T: Route>(value: T) -> T {
    value
}

#[define_opaque(Operation)]
const fn create() -> Operation {
    let _ = retain::<((), Operation)>;
    |value| value + 3
}

struct Ready([u8; core::mem::size_of_val(&create())]);

fn main() {
    let Ready(bytes) = Ready([]);
    assert_eq!(bytes.len(), 0);
    let operation = retain(((), create())).1;
    let copy = operation;
    assert_eq!(operation(17), 20);
    assert_eq!(copy(23), 26);
}
