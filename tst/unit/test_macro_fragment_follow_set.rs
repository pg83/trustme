//@ compile-fail: `$foo:expr` may be followed by `$i:ident`, which is not allowed for `expr` fragments
// A fragment is matched by the parser the language names for it, which stops
// where that parser stops. Only a token that could never have continued the
// fragment may stand next, so `$i:ident` after an `expr` is a rule that could
// never be read the same way twice.

macro_rules! after_expr {
    (start $foo:expr $($i:ident),* end) => {
        $foo
    };
}

fn main() {}
