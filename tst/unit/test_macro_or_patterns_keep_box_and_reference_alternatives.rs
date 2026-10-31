#![feature(box_patterns)]
macro_rules! accepts {
    ($p:pat, $value:expr) => { matches!($value, $p) };
}
fn main() {
    assert!(accepts!(box 1 | box 3, Box::new(3)));
    assert!(!accepts!(box 1 | box 3, Box::new(2)));
    assert!(accepts!(&1 | &3, &3));
    assert!(!accepts!(&1 | &3, &2));
    assert!(accepts!(&&1 | &&3, &&1));
    assert!(accepts!(&mut 1 | &mut 3, &mut 3));
}
