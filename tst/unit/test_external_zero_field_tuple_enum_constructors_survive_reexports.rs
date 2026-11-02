//@ aux-build: value_path_constructors.rs
//@ compile-variant: -O
use value_path_constructors::AliasChoice;
use value_path_constructors::AliasChoice::Empty as build;

fn main() {
    let function: fn() -> AliasChoice = build;
    assert!(matches!(function(), AliasChoice::Empty()));
    assert!(matches!(AliasChoice::Unit, AliasChoice::Unit));
}
