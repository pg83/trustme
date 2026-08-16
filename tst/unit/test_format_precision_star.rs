// `{:.*}` takes the precision from one argument and the value from the next.
// The `*` was recognised but never consumed, so it was then read again as a
// formatting type specifier and rejected.
//
// Same shape as the library test coretests/fmt/mod.rs.
fn main() {
    assert_eq!("1.23", format!("{:.*}", 2, 1.23456f64));
    assert_eq!("1", format!("{:.*}", 0, 1.4f64));

    // The value is the argument after the precision, so a following `{}` picks
    // up the one after that.
    assert_eq!("1.235 x", format!("{:.*} {}", 3, 1.23456f64, "x"));

    // A string is truncated by the same precision.
    assert_eq!("ab", format!("{:.*}", 2, "abcdef"));

    // `.*` alongside a width and a fill.
    assert_eq!("--1.2", format!("{:->5.*}", 1, 1.23f64));

    // The explicitly indexed form of the same thing still works.
    assert_eq!("1.23", format!("{:.1$}", 1.23456f64, 2));
}
