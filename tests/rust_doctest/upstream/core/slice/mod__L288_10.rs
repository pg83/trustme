// Extracted from library/core/src/slice/mod.rs:288
#![allow(unused)]
fn main() {
    let x = &mut [0, 1, 2];

    if let Some(last) = x.last_mut() {
        *last = 10;
    }
    assert_eq!(x, &[0, 1, 10]);

    let y: &mut [i32] = &mut [];
    assert_eq!(None, y.last_mut());
}
