macro_rules! document {
    (#[doc = $text:literal]) => { $text };
    (#![doc = $text:literal]) => { $text };
}
fn main() {
    assert_eq!(document! { /**outer /*nested*/ tail*/ }, "outer /*nested*/ tail");
    assert_eq!(document! { /*!inner /*nested*/ tail*/ }, "inner /*nested*/ tail");
    assert_eq!(document! { /**a/b ** c*/ }, "a/b ** c");
}
