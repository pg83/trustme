// `write!(w, "{}", x)` where `x` writes nothing. rustc's format lowering
// gives `"{}"` the pieces `[""]`: a piece goes before every argument, and a
// trailing one only when literal text follows the last argument.
// `fmt::write` skips an empty piece before an argument but writes the
// trailing one unconditionally. We always appended a trailing piece, so `w`
// saw a `write_str("")`: iri-string's prefix-once writer then emitted its
// `=` for an empty value (`{;empty}` expanded to `;empty=`).
use std::fmt::{self, Write};

struct PrefixOnce<'a> {
    inner: &'a mut String,
    prefix: Option<&'a str>,
    calls: Vec<String>,
}

impl Write for PrefixOnce<'_> {
    fn write_str(&mut self, s: &str) -> fmt::Result {
        self.calls.push(s.to_string());
        if let Some(prefix) = self.prefix.take() {
            self.inner.push_str(prefix);
        }
        self.inner.push_str(s);
        Ok(())
    }
}

struct Encoded<T>(T);

impl<T: fmt::Display> fmt::Display for Encoded<T> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        struct Filter<'a, 'b> {
            writer: &'a mut fmt::Formatter<'b>,
        }
        impl Write for Filter<'_, '_> {
            fn write_str(&mut self, s: &str) -> fmt::Result {
                s.chars().try_for_each(|c| self.write_char(c))
            }
            fn write_char(&mut self, c: char) -> fmt::Result {
                self.writer.write_char(c)
            }
        }
        let mut filter = Filter { writer: f };
        write!(filter, "{}", self.0)
    }
}

fn main() {
    let mut out = String::new();
    let mut writer = PrefixOnce { inner: &mut out, prefix: Some("="), calls: Vec::new() };
    write!(writer, "{}", Encoded("")).unwrap();
    println!("calls {:?}", writer.calls);
    assert!(writer.prefix.is_some());
    assert_eq!(out, "");
    let mut out2 = String::new();
    let mut writer2 = PrefixOnce { inner: &mut out2, prefix: Some("="), calls: Vec::new() };
    write!(writer2, "{}", Encoded("ab")).unwrap();
    assert_eq!(out2, "=ab");
}
