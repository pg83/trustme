// rayon's octillion test runs `two_threads(|| octillion_flat().find_last(..))`
// where `octillion_flat` returns `impl ParallelIterator`. As for a
// where-clause receiver, rustc's probe matches the opaque type's declared
// trait alone; `P: Fn + Sync + Send` is checked after the pick. Ours left the
// one candidate ambiguous while the closure's auto traits waited for its
// captures, and never picked the method.
trait ParIter: Sized + Send {
    type Item: Send;
    fn find_last<P>(self, predicate: P) -> Option<Self::Item>
    where
        P: Fn(&Self::Item) -> bool + Sync + Send;
}

struct Range(u64);
impl ParIter for Range {
    type Item = u64;
    fn find_last<P>(self, predicate: P) -> Option<u64>
    where
        P: Fn(&u64) -> bool + Sync + Send,
    {
        (0..self.0).rev().find(|i| predicate(i))
    }
}

fn octillion() -> impl ParIter<Item = u64> {
    Range(10)
}

fn two_threads<F: Send + FnOnce() -> R, R: Send>(f: F) -> R {
    f()
}

fn main() {
    let x = two_threads(|| octillion().find_last(|_| true));
    assert_eq!(x, Some(9));

}
