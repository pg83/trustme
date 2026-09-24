// An async block is a coroutine, and upstream gives coroutines no builtin
// `Copy` (or `Clone`) candidate.
//@ compile-fail: Failed to find an impl
fn is_copy<T: Copy>(_: &T) -> bool {
    true
}

fn main() {
    let fut = async { 5u8 };
    assert!(is_copy(&fut));
}
