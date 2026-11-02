//@ compile-fail: mismatch
fn require_same<T>(_: T, _: T) {}

fn main() {
    require_same(|| 23u8, || 23u8);
}
