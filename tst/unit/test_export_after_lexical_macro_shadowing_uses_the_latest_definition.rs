macro_rules! selected {
    () => { 7u16 };
}

#[macro_export]
macro_rules! selected {
    () => { 11u16 };
}

fn main() {
    assert_eq!(selected!(), 11);
    assert_eq!(crate::selected!(), 11);
}
