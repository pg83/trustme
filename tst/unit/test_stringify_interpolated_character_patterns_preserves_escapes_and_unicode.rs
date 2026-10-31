macro_rules! spelling { ($value:pat) => { stringify!($value) }; }
fn main() {
    for (actual, expected) in [
        (spelling!('\t'), "'\\t'"),
        (spelling!('\r'), "'\\r'"),
        (spelling!('\n'), "'\\n'"),
        (spelling!('\\'), "'\\\\'"),
        (spelling!('\''), "'\\''"),
        (spelling!('\0'), "'\\0'"),
        (spelling!('\u{1}'), "'\\u{1}'"),
        (spelling!('\u{7f}'), "'\\u{7f}'"),
        (spelling!('é'), "'é'"),
        (spelling!('Ж'), "'Ж'"),
        (spelling!('界'), "'界'"),
        (spelling!('🦀'), "'🦀'"),
    ] { assert_eq!(actual, expected); }
}
