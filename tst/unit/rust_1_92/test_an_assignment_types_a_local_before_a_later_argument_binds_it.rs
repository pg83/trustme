// url's `parse_host` declares `let host_str;`, assigns it a `Cow` in two
// branches, then passes `&host_str` where a `&str` is expected. rustc gives
// the local its type at the first assignment (the value is coerced into the
// place's type as the assignment is checked), so the argument's coercion
// derefs a `Cow<str>`. Making argument bindings wait for every cut the
// coercion checks wait for also held the assignments, which wait for an
// obligation, and the argument bound `host_str: str` first ("Unsized type
// not valid here - str"). Only a pending node holds an argument binding.
use std::borrow::Cow;

fn is_windows_drive_letter(s: &str) -> bool {
    s.len() == 2
}

fn host(input_str: &str, has_ignored_chars: bool) -> (bool, Cow<'_, str>) {
    let host_str;
    {
        let bytes = 1;
        if has_ignored_chars {
            host_str = Cow::Owned(input_str.chars().collect());
        } else {
            host_str = Cow::Borrowed(&input_str[..bytes]);
        }
    }
    if is_windows_drive_letter(&host_str) {
        return (false, "".into());
    }
    (true, host_str)
}

fn main() {
    assert_eq!(host("ab", false).1, "a");
    assert_eq!(host("abc", true).1, "abc");
    assert!(!host("ab", true).0);
}
