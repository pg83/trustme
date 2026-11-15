#![feature(deref_patterns)]

fn main() {
    let values: Vec<_> = Vec::new();
    let empty = match &values {
        [] => true,
        _ => false,
    };
    let _: Vec<u8> = values;
    assert!(empty);
}
