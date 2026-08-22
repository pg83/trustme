trait AstKind {
    type Inner;
}

struct WithSpan;

impl AstKind for WithSpan {
    type Inner = (i32,);
}

struct Expr<'a> {
    field: &'a <WithSpan as AstKind>::Inner,
}

fn check(expr: Expr<'_>) {
    match expr {
        Expr { field: (value,) } => {
            let _: &i32 = value;
        }
    }
}

fn main() {
    let inner = (1,);
    check(Expr { field: &inner });
}
