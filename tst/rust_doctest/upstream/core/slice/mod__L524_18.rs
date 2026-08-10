// Extracted from library/core/src/slice/mod.rs:524
#![allow(unused)]
fn main() {
    let x = &mut [0, 1, 2];

    if let Some(last) = x.last_chunk_mut::<2>() {
        last[0] = 10;
        last[1] = 20;
    }
    assert_eq!(x, &[0, 10, 20]);

    assert_eq!(None, x.last_chunk_mut::<4>());
}
