//@ compile-variant: -O
struct Row<const N: usize>([u16; N]);
fn main() {
    let build = Row::<{ 2 + 1 }>;
    let value = build([11, 17, 23]);
    assert_eq!(value.0, [11, 17, 23]);
}
