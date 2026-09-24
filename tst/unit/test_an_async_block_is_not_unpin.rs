// An async block is an immovable coroutine, and upstream assembles no
// auto-trait candidate for `Unpin` of such a coroutine
// (`assemble_candidates_from_auto_impls`, `Movability::Static`), whatever it
// holds. tokio's async_send_sync asserts `!Unpin` of its futures with an
// ambiguous-if-`Unpin` probe.
//@ compile-fail: Failed to find an impl
//@ edition: 2021
fn is_unpin<T: Unpin>(_: &T) {}

fn main() {
    let boxed = Box::pin(async { 1 });
    is_unpin(&boxed);
    let future = async { 1 };
    is_unpin(&future);
}
