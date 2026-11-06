use std::fmt::Display;

const fn text() -> &'static (impl ?Sized + Display) {
    "seventeen"
}

const POINTER_BYTES: usize = {
    let pointer = text();
    std::mem::size_of_val(&pointer)
};

fn main() {
    assert_eq!(POINTER_BYTES, 2 * std::mem::size_of::<usize>());
    assert_eq!(format!("{}", text()), "seventeen");
}
