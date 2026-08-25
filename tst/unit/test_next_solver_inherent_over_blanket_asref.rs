//@ compile-flags: -Znext-solver
// Method probing under the next solver: `Option<T>: AsRef<_>` has no
// head-matching impl, so the reference blanket `impl AsRef<U> for &mut T
// where T: AsRef<U>` is NoSolution -- not ambiguous -- and must not keep a
// step-0 trait candidate alive that shadows the inherent `Option::as_ref`
// (the `Peekable::peek` pattern).

struct P<I: Iterator> {
    iter: I,
    peeked: Option<Option<I::Item>>,
}

impl<I: Iterator> P<I> {
    fn peek(&mut self) -> Option<&I::Item> {
        let iter = &mut self.iter;
        self.peeked.get_or_insert_with(|| iter.next()).as_ref()
    }
}

fn main() {
    let mut p = P { iter: [1, 2].into_iter(), peeked: None };
    assert_eq!(p.peek(), Some(&1));
    assert_eq!(p.peek(), Some(&1));
    assert_eq!(p.iter.next(), Some(2));
}
