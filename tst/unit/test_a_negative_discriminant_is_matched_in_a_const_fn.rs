// tz-rs 0.7: a const fn matches the `Ordering` another const fn returned.
// `Less` is -1 in an `i8` tag, so the tag byte reads as 255.
use core::cmp::Ordering;

const fn cmp(a: i64, b: i64) -> Ordering {
    if a < b {
        Ordering::Less
    } else if a == b {
        Ordering::Equal
    } else {
        Ordering::Greater
    }
}

const fn min(a: i64, b: i64) -> i64 {
    match cmp(a, b) {
        Ordering::Less | Ordering::Equal => a,
        Ordering::Greater => b,
    }
}

const SMALLER: i64 = min(3, 5);
const LARGER_FIRST: i64 = min(7, 2);

fn main() {
    assert_eq!(SMALLER, 3);
    assert_eq!(LARGER_FIRST, 2);
}
