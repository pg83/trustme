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

fn apply<C: Member>(function: fn(C) -> Cell<C::Item>, value: C) -> Cell<C::Item> {
    function(value)
}

fn main() {
    let result = apply(identity, Cell(97u16));
    assert_eq!(result.0, 97);
    assert_eq!(std::mem::size_of_val(&result), 2);
}
