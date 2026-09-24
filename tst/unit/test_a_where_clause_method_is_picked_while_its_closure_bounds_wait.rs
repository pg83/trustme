// rayon's iter_panic test calls `iter.for_each(|_| ..)` on an
// `impl ParallelIterator + UnwindSafe` parameter inside a `catch_unwind`
// closure. rustc's probe matches a where-clause candidate on the where-clause
// alone (`consider_probe`); the method's own bounds (`OP: Fn + Sync + Send`)
// are obligations of the confirmation. Ours evaluated those bounds in the
// probe, and with the closure's auto traits waiting for its captures the one
// candidate stayed ambiguous: the method was never picked and the capture
// analysis met an unresolved call.
use std::panic::{self, UnwindSafe};
use std::sync::atomic::{AtomicUsize, Ordering};

trait ParIter: Sized + Send {
    type Item: Send;
    fn for_each<OP>(self, op: OP)
    where
        OP: Fn(Self::Item) + Sync + Send;
}

struct Range(i32);
impl ParIter for Range {
    type Item = i32;
    fn for_each<OP>(self, op: OP)
    where
        OP: Fn(i32) + Sync + Send,
    {
        for i in 0..self.0 {
            op(i);
        }
    }
}

fn count(iter: impl ParIter + UnwindSafe) -> usize {
    let count = AtomicUsize::new(0);
    let result = panic::catch_unwind(|| {
        iter.for_each(|_| {
            count.fetch_add(1, Ordering::Relaxed);
        });
    });
    assert!(result.is_ok());
    count.into_inner()
}

fn main() {
    assert_eq!(count(Range(5)), 5);
}
