//@ edition: 2021
// `${concat(...)}` joins identifiers, and a string literal — written directly
// or bound to a metavariable — contributes its contents. Both spellings were
// rejected: the transcriber demanded an identifier.
#![feature(macro_metavar_expr_concat)]

macro_rules! join {
    ($lhs:ident, $rhs:literal) => {
        const ${concat($lhs, $rhs)}: u32 = 7;
    };
}

macro_rules! join_literal {
    ($lhs:ident) => {
        const ${concat($lhs, "_tail")}: u32 = 9;
    };
}

join!(FROM, "_meta");
join_literal!(FROM);

fn main() {
    assert_eq!(FROM_meta, 7);
    assert_eq!(FROM_tail, 9);
}
