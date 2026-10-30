//@ compile-fail: type annotations needed
fn main() {
    let unrelated = 17;
    let callback = Default::default();
    let selected = callback(0);
    let _ = (unrelated, selected);
}
