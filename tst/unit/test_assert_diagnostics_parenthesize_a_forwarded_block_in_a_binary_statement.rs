macro_rules! check {
    ($whole:expr) => { assert!({ $whole }) };
}

macro_rules! equal {
    ($left:expr) => { check!($left == 131u16) };
}

fn main() {
    equal!({ 131u16 });
}
