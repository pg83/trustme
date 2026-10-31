macro_rules! text {
    ($($token:tt)*) => { stringify!($($token)*) };
}

fn main() {
    assert_eq!(stringify!(<<=), "<<=");
    assert_eq!(stringify!(>>=), ">>=");
    assert_eq!(text!(<<=), "<<=");
    assert_eq!(text!(>>=), ">>=");
    let mut bits = 45_u16;
    bits <<= 3;
    bits >>= 2;
    assert_eq!(bits, 90);
}
