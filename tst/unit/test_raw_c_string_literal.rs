// `cr"..."` is a raw C string. The lexer knew `c"..."` and the raw forms of
// the other two literal kinds, but read `cr` as an identifier followed by a
// string.
//
// Same shapes as the Rust Reference examples tokens.md:425 and
// expressions/literal-expr.md:340.
fn main() {
    assert_eq!(c"foo", cr"foo");
    assert_eq!(c"\"foo\"", cr#""foo""#);
    assert_eq!(c"foo #\"# bar", cr##"foo #"# bar"##);
    assert_eq!(cr"foo".count_bytes(), 3);
}
