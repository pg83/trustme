#![feature(decl_macro)]

macro_rules! copies {
    ($item:item) => { mod first { $item } mod second { $item } };
}

copies!(pub mod inner {
    pub macro total {
        () => { 0u16 },
        ($first:expr $(, $rest:expr)*) => { $first $(+ $rest)* },
    }
    pub macro matrix($($($value:expr),*);*) {
        0u16 $($(+ $value)*)*
    }
});

fn main() {
    assert_eq!(first::inner::total!(), 0);
    assert_eq!(second::inner::total!(3, 5, 7), 15);
    assert_eq!(first::inner::matrix!(2, 3; 5), 10);
    assert_eq!(second::inner::matrix!(7; 11, 13), 31);
}
