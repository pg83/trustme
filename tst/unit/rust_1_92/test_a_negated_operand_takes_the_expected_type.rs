// rustc checks the operand of `-` and `!` with the expectation the whole
// expression has (`check_expr_unop` in rustc_hir_typeck/src/expr.rs:
// `hir::UnOp::Not | hir::UnOp::Neg => expected`). prost-derive writes
// `let value: i64 = -lit.base10_parse()?;`: the expected `i64` reaches the
// `?` and makes `base10_parse::<N>`'s `N` an `i64`.
use std::str::FromStr;

struct Lit(&'static str);

impl Lit {
    fn base10_parse<N: FromStr>(&self) -> Result<N, String> {
        self.0.parse().map_err(|_| String::from("not a number"))
    }
}

fn negated(lit: &Lit) -> Result<i64, String> {
    let value: i64 = -lit.base10_parse()?;
    Ok(value)
}

fn inverted(lit: &Lit) -> Result<u8, String> {
    let value: u8 = !lit.base10_parse()?;
    Ok(value)
}

fn main() {
    assert_eq!(negated(&Lit("42")), Ok(-42));
    assert_eq!(inverted(&Lit("15")), Ok(240));
    assert!(negated(&Lit("x")).is_err());
}
