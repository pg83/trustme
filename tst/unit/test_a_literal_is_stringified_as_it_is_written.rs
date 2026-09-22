// A literal token carries the text it was written as (upstream's
// token::Lit symbol), and printing the token prints that text: stringify!
// of `1234000000000000.0` is "1234000000000000.0", not the value
// reformatted as "1.234e+15". ryu's tests compare its output against
// stringify! of the literal.
macro_rules! spelled {
    ($t:tt) => {
        stringify!($t)
    };
}

fn main() {
    assert_eq!(stringify!(1234000000000000.0), "1234000000000000.0");
    assert_eq!(stringify!(1.7976931348623157e308), "1.7976931348623157e308");
    assert_eq!(stringify!(1e10), "1e10");
    assert_eq!(stringify!(2.5f32), "2.5f32");
    assert_eq!(stringify!(1_000.000_1), "1_000.000_1");
    assert_eq!(stringify!(0x__7F_u8), "0x__7F_u8");
    assert_eq!(stringify!(0b1010), "0b1010");
    assert_eq!(stringify!(007), "007");
    assert_eq!(stringify!("a\x41\n"), "\"a\\x41\\n\"");
    assert_eq!(stringify!(r#"raw "x""#), r###"r#"raw "x""#"###);
    assert_eq!(stringify!(b"\x00"), "b\"\\x00\"");
    assert_eq!(spelled!(5e-324), "5e-324");
    assert_eq!(spelled!(0.3), "0.3");
}
