//@ compile-variant: -O
#[derive(Debug, PartialEq, Eq)]
enum Choice<T> { Unit, Tuple(T), EmptyTuple() }
fn invoke<F: FnOnce(u16) -> Choice<u16>>(function: F) -> Choice<u16> { function(17) }
fn main() {
    let unit = &Choice::<u16>::Unit;
    let constructor: fn(u16) -> Choice<u16> = Choice::Tuple;
    let empty: fn() -> Choice<u16> = Choice::EmptyTuple;
    let unit_factory: fn() -> Choice<u16> = || Choice::Unit;
    assert_eq!(invoke(constructor), Choice::Tuple(17));
    assert_eq!(unit_factory(), *unit);
    assert_eq!(empty(), Choice::EmptyTuple());
    assert_ne!(empty(), unit_factory());
}
