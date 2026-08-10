// Extracted from library/core/src/slice/mod.rs:185
#![allow(unused)]
fn main() {
    let x = &[0, 1, 2];

    if let Some((first, elements)) = x.split_first() {
        assert_eq!(first, &0);
        assert_eq!(elements, &[1, 2]);
    }
}
