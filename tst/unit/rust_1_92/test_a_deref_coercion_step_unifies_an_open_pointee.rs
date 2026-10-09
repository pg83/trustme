// `split_at_first(&Cow::Owned(String::new()), "=")` with a `&str`
// parameter: `Cow<?B>` derefs to `?B`, and rustc's `coerce_borrowed_pointer`
// unifies each autoderef step with the target (`unify_raw`), so the step to
// `?B` binds `?B = str`; unsizing is tried once, on the undereferenced types
// (`coerce_unsized`), never at a deref step. We related every step by
// unsizing, which leaves an open source against an unsized target
// ambiguous, so the coercion never settled and fell back to equating
// `&str` with `&Cow<_>`. image's `hdr` decoder tests.
use std::borrow::Cow;

fn split_at_first<'a>(s: &'a str, separator: &str) -> Option<(&'a str, &'a str)> {
    match s.find(separator) {
        None | Some(0) => None,
        Some(p) if p >= s.len() - separator.len() => None,
        Some(p) => Some((&s[..p], &s[(p + separator.len())..])),
    }
}

fn main() {
    assert_eq!(split_at_first(&Cow::Owned(String::new()), "="), None);
    assert_eq!(split_at_first(&Cow::Owned(" = ".into()), "="), Some((" ", " ")));
    assert_eq!(split_at_first(&Cow::Owned("EXPOSURE= ".into()), "="), Some(("EXPOSURE", " ")));
}
