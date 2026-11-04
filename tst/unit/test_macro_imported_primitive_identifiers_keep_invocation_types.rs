macro_rules! make {
    ($name:ident, $primitive:ident, $value:expr) => {
        mod $name {
            use $primitive as Scalar;
            pub fn value() -> Scalar { $value }
        }
    };
}
make!(small, u16, 727);
make!(large, u32, 733);
fn main() {
    assert_eq!(small::value(), 727u16);
    assert_eq!(large::value(), 733u32);
}
