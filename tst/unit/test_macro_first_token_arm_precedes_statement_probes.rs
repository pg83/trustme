macro_rules! select {
    ($($token:tt)*) => { 31u8 };
    ($statement:stmt) => { 99u8 };
}
macro_rules! forward_item { ($item:item) => { select!($item) }; }
fn main() {
    assert_eq!(forward_item!(struct Forwarded;), 31);
    let selected = [
        select!(value),
        select!(value + 1),
        select!(let value = 37),
        select!(union!()),
        select!(auto not_a_trait),
        select!(pub not_an_item),
        select!(42),
        select!(const { 37 }),
        select!(async move { 37 }),
    ];
    assert_eq!(selected, [31; 9]);
}
