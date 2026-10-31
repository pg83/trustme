const LOW: i32 = -3;
const HIGH: i32 = 4;
macro_rules! accepts {
    ($p:pat, $value:expr) => { matches!($value, $p) };
}
mod nested {
    const EDGE: i32 = 7;
    pub fn check() {
        assert!(accepts!(| ..=crate::HIGH, 4));
        assert!(!accepts!(..crate::HIGH, 4));
        assert!(accepts!(crate::LOW..=crate::HIGH, -3));
        assert!(!accepts!(crate::LOW..crate::HIGH, 4));
        assert!(accepts!(| ..=super::HIGH, 3));
        assert!(accepts!(..self::EDGE, 6));
        assert!(!accepts!(super::LOW..=self::EDGE, 8));
    }
}
fn main() { nested::check(); }
