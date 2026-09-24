// tokio's task harness replaces a task's stage with `*ptr = Stage::Consumed`
// under catch_unwind, and the old stage's output may panic in its drop.
// Upstream lowers an assignment over a live value as drop-and-replace: the
// new value is written on the unwind edge of the drop as well
// (`build_drop_and_replace`), so the slot never holds the dropped value.
// Ours left it there, and the next drop dropped it again.
use std::panic::{catch_unwind, AssertUnwindSafe};

struct Once(Option<u8>);

impl Drop for Once {
    fn drop(&mut self) {
        let _ = self.0.take().unwrap();
        panic!("drop panics");
    }
}

fn main() {
    let mut slot = Some(Once(Some(1)));
    let r = catch_unwind(AssertUnwindSafe(|| {
        slot = None;
    }));
    assert!(r.is_err());
    assert!(slot.is_none());
    let mut boxed = Box::new(Once(Some(2)));
    let r = catch_unwind(AssertUnwindSafe(|| {
        *boxed = Once(None);
    }));
    assert!(r.is_err());
    std::mem::forget(boxed);
}
