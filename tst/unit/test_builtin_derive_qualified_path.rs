#[derive(core::clone::Clone)]
struct Value(u32);

fn main() {
    let value = Value(7);
    assert_eq!(value.clone().0, 7);
}
