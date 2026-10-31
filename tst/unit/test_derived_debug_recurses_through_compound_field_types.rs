#[derive(Debug)]
struct Record<'a, T> {
    unit: (),
    pair: (T, T),
    reference: &'a T,
    pointer: *const T,
    array: [T; 2],
    slice: &'a [T],
}
fn main() {
    let source = [37u16, 41];
    let record = Record { unit: (), pair: (3, 5), reference: &source[0], pointer: &source[1], array: [7, 11], slice: &source };
    let text = format!("{:?}", record);
    assert!(text.starts_with("Record { unit: (), pair: (3, 5), reference: 37, pointer: 0x"));
    assert!(text.ends_with(", array: [7, 11], slice: [37, 41] }"));
}
