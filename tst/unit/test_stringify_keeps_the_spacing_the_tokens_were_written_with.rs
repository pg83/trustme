// `stringify!` is `pprust::tts_to_string`: a space goes between two tokens
// only when the first was followed by whitespace in the source (its
// `Spacing` is `Alone`) and `space_between` allows one, and lines are broken
// by the pretty printer at 78 columns. Literals keep their spelling. Tokens
// a macro_rules body writes out are `Alone`, as upstream transcribes them.
macro_rules! show {
    ($($t:tt)*) => {
        stringify!($($t)*)
    };
}

macro_rules! body {
    ($x:ident) => {
        stringify!(a+b,c $x+$x)
    };
}

fn main() {
    assert_eq!(stringify!(a+b), "a+b");
    assert_eq!(stringify!(a + b), "a + b");
    assert_eq!(stringify!(f(a,b)), "f(a,b)");
    assert_eq!(stringify!(f (a, b)), "f(a, b)");
    assert_eq!(stringify!(struct S { a: u8 }), "struct S { a: u8 }");
    assert_eq!(stringify!('\n' b'x' 'a' 1u8 0x1F r#"raw"#), r####"'\n' b'x' 'a' 1u8 0x1F r#"raw"#"####);
    assert_eq!(stringify!(fn foo<'a>(x: &'a str) -> Option<&'a str> {}), "fn foo<'a>(x: &'a str) -> Option<&'a str> {}");
    assert_eq!(stringify!(a::<B>::c && !d), "a::<B>::c && !d");
    assert_eq!(stringify!(Self(1) fn(x) pub(crate) true(x) _(x) foo!()), "Self(1) fn(x) pub(crate) true(x) _(x) foo!()");
    assert_eq!(stringify!(#![allow(x)] # [a]), "#![allow(x)] #[a]");
    assert_eq!(stringify!(a,b ;c ,d), "a,b;c,d");
    assert_eq!(stringify!(x?.y? .z), "x?.y? .z");
    assert_eq!(stringify!(a/*c*/b // d
        c), "a b c");
    assert_eq!(show!(a+b c . d), "a+b c.d");
    assert_eq!(body!(x), "a + b, c x + x");
    assert_eq!(
        stringify!(aaaaaaaaaaaaaaaaaaaaaaa bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb ccccccccccccccccccccccccccccccc ddddddddddddddddddddddddd),
        "aaaaaaaaaaaaaaaaaaaaaaa bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\nccccccccccccccccccccccccccccccc ddddddddddddddddddddddddd"
    );
    assert_eq!(
        stringify!({ let x = 1; aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa + bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb }),
        "{\n    let x = 1; aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa +\n    bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\n}"
    );
}
