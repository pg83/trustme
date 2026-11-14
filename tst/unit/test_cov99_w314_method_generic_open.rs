//@ compile-fail: Unexpected token TOK_PAREN_OPEN, expected TOK_LT

fn main() {
    ().method::();
}
