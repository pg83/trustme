macro_rules! accepts {
    ($p:pat, $value:expr) => { matches!($value, $p) };
}
fn main() {
    assert!(accepts!(| ..=-4 | 2..=4, -5));
    assert!(accepts!(..-4 | 2..4, 3));
    assert!(!accepts!(| ..=-4 | 2..=4, 0));
    assert!(accepts!(-8..=-3, -4));
    assert!(!accepts!(-8..-3, -3));
    assert!(accepts!(| ..='F' | 'a'..='f', 'B'));
    assert!(accepts!(..'F' | 'a'..'f', 'b'));
    assert!(!accepts!(..'F' | 'a'..'f', 'F'));
    assert!(accepts!('a'..='z', 'z'));
    assert!(!accepts!('a'..'z', 'z'));
}
