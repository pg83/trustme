//@ compile-variant: -O
fn extract<T>(value: Option<T>) -> T { value.unwrap() }
fn finish() -> u16 { 97 }
fn require(_: fn() -> u16) {}
fn unused() {
    let source = extract(None);
    let result = source as fn() -> u16;
    require(source);
    std::hint::black_box(result);
}
fn main() {
    let source = extract(Some(finish as fn() -> u16));
    let result = source as fn() -> u16;
    assert_eq!(result(), 97);
    let _ = unused as fn();
}
