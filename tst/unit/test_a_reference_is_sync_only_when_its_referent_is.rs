// An auto trait looks through a reference, a raw pointer and a slice to
// the type behind it (upstream `constituent_types_for_auto_trait`).
// futures' auto_traits asserts `oneshot::Cancellation<'_, *const ()>` is
// not `Sync`; it holds `&mut Sender<*const ()>`.
//@ compile-fail: Failed to find an impl
use std::cell::Cell;

fn need<T: ?Sized + Sync>() {}

fn main() {
    need::<&mut u8>();
    need::<[&u8]>();
    need::<&mut Cell<u8>>();
}
