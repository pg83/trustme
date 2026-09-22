// time's `version!(3..)`: a `$pat:pat` fragment that is a half-open range.
// The matcher's pattern scanner took `3` and stopped at `..`, so no arm
// matched. Upstream's pattern grammar allows a range with either end
// missing: `lo..`, `..hi`, `..=hi`, and `lo..hi` since 1.80.
const VERSION: u8 = 3;
macro_rules! version {
    ($pat:pat) => {
        matches!(VERSION, $pat)
    };
}
fn main() {
    assert!(version!(3..));
    assert!(!version!(4..));
    assert!(version!(1..=3));
    assert!(version!(..4));
    assert!(!version!(..3));
    assert!(version!(2..4));
    assert!(version!(2 | 3));
    assert!(version!(1.. | 0));
}
