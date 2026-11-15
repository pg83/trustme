//@ edition: 2021

macro_rules! nested_tuple {
    () => { 7u8 };
    (@ $($rest:tt)*) => { (nested_tuple!($($rest)*),) };
}

fn main() {
    // Each marker adds one tuple layer; the byte is at promotion depth 65.
    let value: &'static _ = &nested_tuple!(
        @ @ @ @ @ @ @ @ @ @ @ @ @
        @ @ @ @ @ @ @ @ @ @ @ @ @
        @ @ @ @ @ @ @ @ @ @ @ @ @
        @ @ @ @ @ @ @ @ @ @ @ @ @
        @ @ @ @ @ @ @ @ @ @ @ @ @
    );
    assert_eq!(std::mem::size_of_val(value), 1);
}
