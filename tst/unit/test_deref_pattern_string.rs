// A string literal pattern matched against a `String` derefs to `str`, so that
// arm carries a rule for a root of its own while its neighbours destructure the
// string. Arms of a match may therefore disagree on how many rules they hold.
#![feature(deref_patterns)]

fn describe(o: Option<String>) -> &'static str {
    match o {
        Some("42") => "the answer",
        Some(_) => "something else",
        None => "nil",
    }
}

fn main() {
    assert_eq!(describe(Some(String::from("42"))), "the answer");
    assert_eq!(describe(Some(String::new())), "something else");
    assert_eq!(describe(None), "nil");
}
