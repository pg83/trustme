//@ compile-fail: type annotations needed
fn main() {
    let items = std::iter::empty::<u8>().collect();
    let _ = &items;
}
