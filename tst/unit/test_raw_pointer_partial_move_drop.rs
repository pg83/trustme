//@ test-harness

use std::sync::atomic::{AtomicUsize, Ordering};

static DROPPED: AtomicUsize = AtomicUsize::new(0);

struct DropBit(usize);

impl Drop for DropBit {
    fn drop(&mut self) {
        DROPPED.fetch_or(self.0, Ordering::SeqCst);
    }
}

#[test]
fn raw_pointer_write_does_not_restore_drop_state() {
    DROPPED.store(0, Ordering::SeqCst);

    {
        let mut value = None;
        let raw: *mut Option<(DropBit, DropBit)> = &mut value;

        match value {
            None => (),
            _ => return,
        }

        unsafe { *raw = Some((DropBit(1), DropBit(2))) };

        match value {
            Some((_moved, _)) => (),
            _ => (),
        }
    }

    assert_eq!(DROPPED.load(Ordering::SeqCst), 1);
}
