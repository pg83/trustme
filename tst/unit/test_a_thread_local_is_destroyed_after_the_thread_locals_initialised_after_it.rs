//@ edition: 2021
// tokio's `LocalSet` in a `thread_local!` checks, when dropped, that the
// thread id kept in tokio's own `CONTEXT` thread-local is its owner's; the
// check skips when `CONTEXT` is already gone. On x86_64-linux rustc sets
// `cfg(target_thread_local)`, so std stores thread-locals in `#[thread_local]`
// statics and runs their destructors last registered, first run: `CONTEXT`,
// touched while the `LocalSet` was built, outlives it. We did not set the
// cfg; std fell back to pthread keys, which destroy in key-creation order
// and re-create a value touched after its destruction.
use std::cell::{Cell, RefCell};
use std::sync::atomic::{AtomicU64, Ordering};

struct Context {
    id: Cell<Option<u64>>,
    _held: RefCell<Option<Box<u8>>>,
}

thread_local! {
    static CONTEXT: Context = const { Context { id: Cell::new(None), _held: RefCell::new(None) } };
}

static NEXT: AtomicU64 = AtomicU64::new(1);

fn thread_id() -> Result<u64, std::thread::AccessError> {
    CONTEXT.try_with(|ctx| match ctx.id.get() {
        Some(id) => id,
        None => {
            let id = NEXT.fetch_add(1, Ordering::Relaxed);
            ctx.id.set(Some(id));
            id
        }
    })
}

struct Owner(u64);

impl Drop for Owner {
    fn drop(&mut self) {
        let now = thread_id();
        assert!(now.map(|id| id == self.0).unwrap_or(true));
    }
}

thread_local! {
    static OWNER: Owner = Owner(thread_id().unwrap());
}

fn main() {
    let _ = thread_id();
    std::thread::spawn(|| OWNER.with(|_| ())).join().unwrap();
}
