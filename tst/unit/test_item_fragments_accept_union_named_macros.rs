macro_rules! union {
    ($name:ident, $value:expr) => { fn $name() -> u32 { $value } };
}
macro_rules! items { ($($item:item)*) => { $($item)* }; }
items! {
    union!(paren, 17);
    union! { brace, 23 }
    union![square, 31];
}
fn main() { assert_eq!((paren(), brace(), square()), (17, 23, 31)); }
