//@ edition: 2021

trait Marker {}
impl Marker for u8 {}
fn accept<T: Marker>(_: T) {}

macro_rules! site {
    () => {{
        let [left, right] = unsafe { std::mem::zeroed() };
        let _: u8 = left;
        let _: u8 = right;
        accept(left);
        assert_eq!(right, 0);
    }};
}

macro_rules! repeat {
    () => { site!(); };
    ($one:tt $($rest:tt)*) => {
        repeat!($($rest)*);
        repeat!($($rest)*);
    };
}

fn main() {
    repeat!(x x x x x x x x x x);
}
