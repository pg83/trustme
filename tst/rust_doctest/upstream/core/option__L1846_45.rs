// Extracted from library/core/src/option.rs:1846
#![allow(unused)]
fn main() {
    let mut x = Some(42);

    let prev = x.take_if(|v| if *v == 42 {
        *v += 1;
        false
    } else {
        false
    });
    assert_eq!(x, Some(43));
    assert_eq!(prev, None);

    let prev = x.take_if(|v| *v == 43);
    assert_eq!(x, None);
    assert_eq!(prev, Some(43));
}
