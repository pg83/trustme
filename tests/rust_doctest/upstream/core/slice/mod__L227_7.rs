// Extracted from library/core/src/slice/mod.rs:227
#![allow(unused)]
fn main() {
    let x = &[0, 1, 2];

    if let Some((last, elements)) = x.split_last() {
        assert_eq!(last, &2);
        assert_eq!(elements, &[0, 1]);
    }
}
