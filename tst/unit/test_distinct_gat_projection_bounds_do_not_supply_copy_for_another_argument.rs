//@ compile-fail: Failed to find an impl of
trait Source { type Cell<T>; }
trait Left: Source<Cell<u8>: Copy> {}
trait Right: Source<Cell<String>: Eq> {}
trait Both: Left + Right {}

fn require_copy<T: Copy>(_: T) {}
fn check<I: Both>(text: I::Cell<String>) {
    require_copy(text);
}
fn main() {}
