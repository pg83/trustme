#[derive(Clone, Copy)]
struct Entry(u16);

fn main() {
    let value = if cfg!(any()) { 0 } else { 457u16 };
    let original = Entry(value);
    let copied = original.clone();
    assert_eq!(original.0, 457);
    assert_eq!(copied.0, 457);
}
