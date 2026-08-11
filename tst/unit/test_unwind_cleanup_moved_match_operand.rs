//@ compile-flags: -O

#[derive(Debug, Eq, PartialEq)]
enum Bound<T> {
    Included(T),
    Excluded(T),
    Unbounded,
}

use Bound::{Excluded, Included, Unbounded};

#[inline(never)]
fn intersect<T: Ord>(
    self_start: Bound<T>,
    self_end: Bound<T>,
    other_start: Bound<T>,
    other_end: Bound<T>,
) -> (Bound<T>, Bound<T>) {
    let start = match (self_start, other_start) {
        (Included(a), Included(b)) => Included(Ord::max(a, b)),
        (Excluded(a), Excluded(b)) => Excluded(Ord::max(a, b)),
        (Unbounded, Unbounded) => Unbounded,
        (x, Unbounded) | (Unbounded, x) => x,
        (Included(i), Excluded(e)) | (Excluded(e), Included(i)) => {
            if i > e { Included(i) } else { Excluded(e) }
        }
    };
    let end = match (self_end, other_end) {
        (Included(a), Included(b)) => Included(Ord::min(a, b)),
        (Excluded(a), Excluded(b)) => Excluded(Ord::min(a, b)),
        (Unbounded, Unbounded) => Unbounded,
        (x, Unbounded) | (Unbounded, x) => x,
        (Included(i), Excluded(e)) | (Excluded(e), Included(i)) => {
            if i < e { Included(i) } else { Excluded(e) }
        }
    };
    (start, end)
}

fn main() {
    let result = intersect(
        Included(String::from("b")),
        Excluded(String::from("z")),
        Unbounded,
        Included(String::from("y")),
    );
    assert_eq!(
        result,
        (Included(String::from("b")), Included(String::from("y")))
    );
}
