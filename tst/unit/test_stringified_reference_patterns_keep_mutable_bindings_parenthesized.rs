macro_rules! bind {
    ($pattern:pat, $value:expr, $body:expr) => {{
        let spelling = stringify!($pattern);
        let $pattern = $value;
        $body;
        spelling
    }};
}
fn main() {
    let original = 311u16;
    let spelling = bind!(&(mut copy), &original, {
        copy += 2;
        assert_eq!(copy, 313);
    });
    let compact: String = spelling.chars().filter(|c| !c.is_whitespace()).collect();
    assert_eq!(compact, "&(mutcopy)");
    assert_eq!(original, 311);
}
