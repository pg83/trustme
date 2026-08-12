//@ edition: 2024

macro_rules! value {
    ($expr:expr) => {
        $expr
    };
}

fn main() {
    assert_eq!(value!(const { 4 }), 4);
}
