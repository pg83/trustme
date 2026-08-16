// `{:.0}` is a precision of zero, not an omitted precision. It used to compare
// equal to "no formatting arguments at all", which took the simple
// `Arguments::new_v1` path and dropped the precision entirely, so `{:.0}` of a
// float printed every digit.
//
// Same shapes as the library tests coretests/fmt/float.rs.
fn main() {
    assert_eq!("9", format!("{:.0}", 9.4f64));
    assert_eq!("10", format!("{:.0}", 9.9f64));
    assert_eq!("1", format!("{:.0}", 1.0f32));

    // Ties round to even, which only shows once the precision is applied.
    assert_eq!("0", format!("{:.0}", 0.5f64));
    assert_eq!("2", format!("{:.0}", 1.5f64));

    // A non-zero precision still works, and so does precision on a string.
    assert_eq!("9.8", format!("{:.1}", 9.849f64));
    assert_eq!("", format!("{:.0}", "abc"));
    assert_eq!("ab", format!("{:.2}", "abc"));
}
