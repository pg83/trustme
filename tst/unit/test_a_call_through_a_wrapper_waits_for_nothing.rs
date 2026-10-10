// rocket's `server::handle` wraps its handler, `let run = AssertUnwindSafe(run);`,
// and calls it in a closure, `catch_unwind(move || run())`. Upstream checks the
// call as `<AssertUnwindSafe<?T> as FnOnce<()>>::call_once` the moment it meets
// it (`try_overloaded_call_traits`, rustc_hir_typeck/src/callee.rs): the
// obligation `AssertUnwindSafe<?T>: FnOnce<()>` may hold, so it is registered
// and the call's type is the projected `Output`; `?T` is `F` from the argument.
// We waited for the impl's nested `?T: FnOnce<()>` to be proven and, every pass
// it was not, applied the ambiguous answer again - a fresh variable and a new
// projection rule each time - so the pass never came out unchanged, the
// argument never bound `?T`, and typeck gave up after 5000 passes.
use std::panic::AssertUnwindSafe;

fn handle<T, F>(run: F) -> Option<T>
where
    F: FnOnce() -> T,
{
    let run = AssertUnwindSafe(run);
    std::panic::catch_unwind(move || run()).ok()
}

fn main() {
    assert_eq!(handle(|| 7), Some(7));
}
