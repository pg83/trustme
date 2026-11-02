//@ aux-build: value_path_constructors.rs
//@ compile-variant: -O
use value_path_constructors::{Unit, Empty, AliasEmpty};

fn main() {
    let first: fn() -> Empty = Empty;
    let second: fn() -> AliasEmpty = AliasEmpty;
    let unit = Unit;
    assert_eq!(std::mem::size_of_val(&unit), 0);
    assert_eq!(std::mem::size_of_val(&first()), 0);
    assert_eq!(std::mem::size_of_val(&second()), 0);
    assert_eq!(std::mem::size_of_val(&Empty), 0);
}
