// `let mut arg_expr;` assigned and moved into a `Box` on every iteration of
// a loop (equator-macro). Its drop after the loop switches on the
// discriminant only under the enum's own flag, as rustc's drop ladder
// tests the rest path's flag before the switch. Once the optimiser forwarded
// the only assignment into the tuple, the local was never assigned, and the
// garbage collector took the guarded switch for a read of an uninitialised
// binding. Such a switch is dead, like a drop of the local already was.
#[derive(Debug)]
enum AssertExpr {
    Bool(String),
    And(Box<(AssertExpr, AssertExpr)>),
}

fn handle(n: usize, id: usize) -> (AssertExpr, usize) {
    (AssertExpr::Bool(format!("e{n}")), id + 1)
}

fn build(mut args: Vec<usize>) -> (AssertExpr, usize) {
    let mut id = 0;
    let mut assert_expr;
    let mut arg_expr;
    (assert_expr, id) = handle(args.pop().unwrap(), id);
    while let Some(arg) = args.pop() {
        (arg_expr, id) = handle(arg, id);
        assert_expr = AssertExpr::And(Box::new((arg_expr, assert_expr)));
    }
    (assert_expr, id)
}

fn main() {
    let (e, id) = build(vec![1, 2, 3]);
    assert_eq!(id, 3);
    assert_eq!(format!("{e:?}"), r#"And((Bool("e1"), And((Bool("e2"), Bool("e3")))))"#);
}
