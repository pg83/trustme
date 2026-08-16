// A bound list may end with `+`, and the lexer's maximal `+=` is a separator
// followed by a default's `=`. Both spellings were rejected.
use std::fmt::Debug;

struct Whitespace<T: Clone + = ()> {
    t: T,
}

struct TokenSplit<T: Clone +=  ()> {
    t: T,
}

fn main() {
    let x: Box<dyn Debug+> = Box::new(3) as Box<dyn Debug+>;
    assert_eq!(format!("{:?}", x), "3");

    let a: Whitespace<()> = Whitespace { t: () };
    let b: TokenSplit<()> = TokenSplit { t: () };
    let _ = (a.t, b.t);
}
