//@ compile-fail: Unexpected token TOK_PLUS
macro_rules! preserve {
    ($whole:expr) => {{
        let spelling = stringify!($whole);
        assert!(spelling.contains("113"));
        assert!(spelling.contains('+'));
        $whole
    }};
}

macro_rules! add {
    ($left:expr) => { preserve!({ $left + 2u16 }) };
}

fn main() {
    assert_eq!(add!({ 113u16 }), 115u16);
}
