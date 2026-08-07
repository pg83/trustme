// Extracted from library/core/src/array/mod.rs:556
#![allow(unused)]
#![feature(array_try_map)]
fn main() {

    let a = ["1", "2", "3"];
    let b = a.try_map(|v| v.parse::<u32>()).unwrap().map(|v| v + 1);
    assert_eq!(b, [2, 3, 4]);

    let a = ["1", "2a", "3"];
    let b = a.try_map(|v| v.parse::<u32>());
    assert!(b.is_err());

    use std::num::NonZero;

    let z = [1, 2, 0, 3, 4];
    assert_eq!(z.try_map(NonZero::new), None);

    let a = [1, 2, 3];
    let b = a.try_map(NonZero::new);
    let c = b.map(|x| x.map(NonZero::get));
    assert_eq!(c, Some(a));
}
