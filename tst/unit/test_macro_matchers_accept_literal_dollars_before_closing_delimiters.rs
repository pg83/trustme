macro_rules! literal {
    ($) => { 149u16 };
    ([$]) => { 151u16 };
}
fn main() {
    assert_eq!(literal!($), 149);
    assert_eq!(literal!([$]), 151);
}
