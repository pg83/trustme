//@ compile-fail: error
fn pair() -> (impl Copy, impl Copy) { (239u8, 241u8) }
fn same<T: Copy>(_: T, _: T) {}
fn main() {
    let (first, second) = pair();
    same(first, second);
}
