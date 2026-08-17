// `concat!` and `stringify!` report a float literal as written, and a float
// literal always carries a decimal point. It was rendered with the default
// float formatting, so `4.0` came out as `4` -- which is not even a float when
// the text is reparsed.
//
// Same shape as the upstream test macros/concat-rpass.rs.
fn main() {
    assert_eq!(concat!(1, 2, 3, 4f32, 4.0, 'a', true), "12344.0atrue");
    assert_eq!(concat!(4.0), "4.0");
    assert_eq!(concat!(0.5), "0.5");
    assert_eq!(concat!(-1.0), "-1.0");

    assert_eq!(stringify!(4.0), "4.0");
    assert_eq!(stringify!(0.25), "0.25");

    // Integers are unchanged.
    assert_eq!(concat!(4), "4");
    assert_eq!(stringify!(4), "4");
}
