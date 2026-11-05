//@ aux-build: single_opaque_compatibility_provider.rs
//@ compile-variant: -O

fn main() {
    let value = single_opaque_compatibility_provider::make();
    assert_eq!(std::mem::size_of_val(&value), 4);
    let copied = value;
    assert_eq!(std::mem::size_of_val(&copied), 4);
}
