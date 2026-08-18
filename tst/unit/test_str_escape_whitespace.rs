// A line continuation skips space, tab, CR and LF -- not every character a
// locale would call whitespace.
fn main() {
    let s = "a\
 b";
    assert_eq!(s, "ab");

    let s = "a\
	b";
    assert_eq!(s, "ab");

    // A form feed is ASCII whitespace but is not skipped.
    let s = b"a\
    \x0cb";
    assert_eq!(s, b"a\x0cb");

    // Nor is a no-break space.
    let s = "a\
    \u{a0}b";
    assert_eq!(s, "a\u{a0}b");

    let s = "\

             ";
    assert_eq!(s, "");
}
