#![feature(return_type_notation)]

trait Factory {
    fn make() -> impl Copy;
}

macro_rules! spell {
    ($ty:ty) => {
        stringify!($ty)
    };
}

fn main() {
    let spelling = spell!(impl Factory<make(..): Copy>);
    assert_eq!(spelling.split_whitespace().collect::<String>(), "implFactory<make(..):Copy>");
}
