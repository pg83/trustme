//@ compile-fail: first
#![feature(decl_macro)]

macro_rules! copies {
    ($item:item) => { mod first { $item } mod second { $item } };
}

copies!(pub macro total {
    () => { 0u16 },
    ($first:expr $(, $rest:expr)*) => { $first $(+ $rest)* },
});

copies!(pub macro matrix($($($value:expr),*);*) {
    0u16 $($(+ $value)*)*
});

fn main() {
    assert_eq!(first::total!(), 0);
    assert_eq!(second::total!(3, 5, 7), 15);
    assert_eq!(first::matrix!(2, 3; 5), 10);
    assert_eq!(second::matrix!(7; 11, 13), 31);
}
