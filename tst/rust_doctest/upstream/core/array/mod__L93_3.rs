// Extracted from library/core/src/array/mod.rs:93
#![allow(unused)]
fn main() {
    // TBH `array::repeat` would be better for this, but it's not stable yet.
    let my_string = String::from("Hello");
    let clones: [String; 42] = std::array::from_fn(|_| my_string.clone());
    assert!(clones.iter().all(|x| *x == my_string));
}
