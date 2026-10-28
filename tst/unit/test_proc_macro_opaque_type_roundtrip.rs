//@ proc-macro-aux-build: proc_macro_item_passthrough.rs
use proc_macro_item_passthrough::echo;

macro_rules! produce {
    ($name:ident, $ty:ty, $body:expr) => {
        echo! { fn $name() -> $ty { $body } }
    };
}
macro_rules! consume {
    ($ty:ty) => {
        echo! { fn apply(function: $ty, value: &u32) -> u32 { *function(value) } }
    };
}
macro_rules! spell {
    ($ty:ty) => { stringify!($ty) };
}

produce!(values, impl Iterator<Item = u32> + Clone + Send + 'static, [3, 5, 8].into_iter());
produce!(borrowed, &'static (impl AsRef<str> + ?Sized + 'static), "borrowed");
consume!(impl for<'a> Fn(&'a u32) -> &'a u32);

fn main() {
    let spelling = spell!(impl AsRef<str> + Send + ?Sized + 'static);
    assert!(spelling.starts_with("impl AsRef<str>"));
    assert!(spelling.contains("+ Send"));
    assert!(spelling.contains("?Sized"));
    assert!(spelling.ends_with("'static"));
    let values = values();
    assert_eq!(values.clone().sum::<u32>(), 16);
    assert_eq!(values.count(), 3);
    assert_eq!(borrowed().as_ref(), "borrowed");
    assert_eq!(apply(|value| value, &19), 19);
}
