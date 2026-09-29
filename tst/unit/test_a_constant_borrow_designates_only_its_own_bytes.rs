//@ run-pass
// schemars trims a doc string in a `const fn` with `while let [rest @ .., last]`
// and reads the result through `{ const TITLE: &str = ..; TITLE }`. The
// constant is a pointer into the literal's allocation plus a length, and rustc
// emits exactly that. A borrow constant was turned into a literal of every
// byte from its offset to the end of the allocation, so `const T: &[u8] =
// trim_end(b"xy \n")` read back as `b"xy \n"`, and a `&[u16]` got its length
// in bytes.
const fn trim_end(mut bytes: &[u8]) -> &[u8] {
    while let [rest @ .., last] = bytes {
        if last.is_ascii_whitespace() {
            bytes = rest;
        } else {
            break;
        }
    }
    bytes
}

const fn trim_str(text: &str) -> &str {
    match core::str::from_utf8(trim_end(text.as_bytes())) {
        Ok(text) => text,
        Err(_) => panic!("invalid UTF-8"),
    }
}

const fn tail_words(words: &[u16]) -> &[u16] {
    match words {
        [_, rest @ ..] => rest,
        [] => words,
    }
}

const fn middle(bytes: &[u8; 4]) -> &[u8; 2] {
    let [_, middle @ .., _] = bytes;
    middle
}

fn main() {
    let bytes = {
        const BYTES: &[u8] = trim_end(b"xy \n");
        BYTES
    };
    let text = {
        const TEXT: &str = trim_str("de \n");
        TEXT
    };
    let words = {
        const WORDS: &[u16] = tail_words(&[1, 2, 3]);
        WORDS
    };
    let middle = {
        const MIDDLE: &[u8; 2] = middle(&[1, 2, 3, 4]);
        MIDDLE
    };
    assert_eq!(bytes, b"xy");
    assert_eq!(text, "de");
    assert_eq!(words, [2, 3]);
    assert_eq!(middle, &[2, 3]);
}
