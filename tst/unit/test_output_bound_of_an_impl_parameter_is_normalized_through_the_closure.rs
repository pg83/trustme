//@ run-pass
/* rayon's `fold_is_full` test: `(0..2048).into_par_iter().inspect(|_| ..).fold(|| 0, |a, b| a + b)
   .find_any(|_| true)` - `find_any` is looked up on `Fold<Inspect<..>, closure, closure>`, whose
   `ParallelIterator` impl names `U` only in its where-clauses: `F: Fn(U, I::Item) -> U`,
   `ID: Fn() -> U`, `U: Send`.  Upstream instantiates `U` as an inference variable, the closure's
   signature `(?0, i32) -> ?0` decides it through `F: Fn<(?U, i32)>`, and the `Output = U`
   projection predicate normalizes to the same variable.  Relating that requirement to the raw
   alias `<F as FnOnce<(U, i32)>>::Output` bound `U` to an alias over its own existential, which
   `U: Send` could not normalize: "Failed to find an impl of ParallelIterator". */
use std::sync::atomic::{AtomicUsize, Ordering};

trait ParallelIterator: Sized + Send {
    type Item: Send;

    fn drive(self) -> Vec<Self::Item>;

    fn inspect<OP>(self, inspect_op: OP) -> Inspect<Self, OP>
    where
        OP: Fn(&Self::Item) + Sync + Send,
    {
        Inspect { base: self, inspect_op }
    }

    fn fold<T, ID, F>(self, identity: ID, fold_op: F) -> Fold<Self, ID, F>
    where
        F: Fn(T, Self::Item) -> T + Sync + Send,
        ID: Fn() -> T + Sync + Send,
        T: Send,
    {
        Fold { base: self, identity, fold_op }
    }

    fn find_any<P>(self, predicate: P) -> Option<Self::Item>
    where
        P: Fn(&Self::Item) -> bool + Sync + Send,
    {
        self.drive().into_iter().find(|item| predicate(item))
    }
}

struct Iter<T> {
    range: std::ops::Range<T>,
}

impl ParallelIterator for Iter<i32> {
    type Item = i32;
    fn drive(self) -> Vec<i32> {
        self.range.collect()
    }
}

struct Inspect<I, F> {
    base: I,
    inspect_op: F,
}

impl<I, F> ParallelIterator for Inspect<I, F>
where
    I: ParallelIterator,
    F: Fn(&I::Item) + Sync + Send,
{
    type Item = I::Item;
    fn drive(self) -> Vec<I::Item> {
        let items = self.base.drive();
        for item in &items {
            (self.inspect_op)(item);
        }
        items
    }
}

struct Fold<I, ID, F> {
    base: I,
    identity: ID,
    fold_op: F,
}

impl<U, I, ID, F> ParallelIterator for Fold<I, ID, F>
where
    I: ParallelIterator,
    F: Fn(U, I::Item) -> U + Sync + Send,
    ID: Fn() -> U + Sync + Send,
    U: Send,
{
    type Item = U;
    fn drive(self) -> Vec<U> {
        let mut acc = (self.identity)();
        for item in self.base.drive() {
            acc = (self.fold_op)(acc, item);
        }
        vec![acc]
    }
}

fn main() {
    let counter = AtomicUsize::new(0);
    let a = Iter { range: 0_i32..2048 }
        .inspect(|_| {
            counter.fetch_add(1, Ordering::SeqCst);
        })
        .fold(|| 0, |a, b| a + b)
        .find_any(|_| true);
    assert_eq!(a, Some(2047 * 1024));
    assert_eq!(counter.load(Ordering::SeqCst), 2048);
}
