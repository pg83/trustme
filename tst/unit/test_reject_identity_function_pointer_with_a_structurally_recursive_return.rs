//@ compile-fail: Type mismatch between

trait Member {
    type Item;
}

struct Cell<T>(T);

impl<T> Member for Cell<T> {
    type Item = T;
}

fn identity<T>(value: T) -> T {
    value
}

fn require<C: Member>(_: fn(C) -> (C, Cell<C::Item>), _: C) {}

fn main() {
    require(identity, Cell(101u16));
}
