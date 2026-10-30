//@ compile-fail: type annotations needed
fn main() {
    let unrelated = 17;
    let values = Default::default();
    let selected = values[0];
    let _ = (unrelated, selected);
}
