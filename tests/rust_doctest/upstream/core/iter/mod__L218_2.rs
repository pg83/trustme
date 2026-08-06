// Extracted from library/core/src/iter/mod.rs:218
#![allow(unused)]
fn main() {
    let mut values = vec![41];
    for x in values.iter_mut() {
        *x += 1;
    }
    for x in values.iter() {
        assert_eq!(*x, 42);
    }
    assert_eq!(values.len(), 1); // `values` is still owned by this function.
}
