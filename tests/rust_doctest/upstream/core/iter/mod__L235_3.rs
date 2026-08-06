// Extracted from library/core/src/iter/mod.rs:235
#![allow(unused)]
fn main() {
    let mut values = vec![41];
    for x in &mut values { // same as `values.iter_mut()`
        *x += 1;
    }
    for x in &values { // same as `values.iter()`
        assert_eq!(*x, 42);
    }
    assert_eq!(values.len(), 1);
}
