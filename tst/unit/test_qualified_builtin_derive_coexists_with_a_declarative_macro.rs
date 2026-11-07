macro_rules! Clone {
    ($value:expr) => { $value + 1 };
}

#[derive(::core::clone::Clone)]
struct Entry(u16);

fn main() {
    let original = Entry(Clone!(439u16));
    let copied = original.clone();
    assert_eq!(copied.0, 440);
    assert_eq!(original.0, 440);
}
