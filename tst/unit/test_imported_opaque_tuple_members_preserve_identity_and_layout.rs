//@ aux-build: indexed_opaque_tuple_provider.rs
//@ compile-variant: -O

fn main() {
    let pair = indexed_opaque_tuple_provider::make();
    assert_eq!(indexed_opaque_tuple_provider::sizes(&pair), (1, 2));
    assert_eq!(std::mem::size_of_val(&pair.0), 1);
    assert_eq!(std::mem::size_of_val(&pair.1), 2);
    let copied = pair.0;
    let cloned = pair.1.clone();
    assert_eq!(std::mem::size_of_val(&copied), 1);
    assert_eq!(std::mem::size_of_val(&cloned), 2);
}
