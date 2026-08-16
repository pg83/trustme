// `|_||x, y| x + y` returns a closure. The lexer joins the `|` that closes the
// outer parameter list with the `|` that opens the inner one, so the parameter
// list ended on a token the parser did not expect.
//
// Same shape as the upstream test
// functions-closures/closure-returning-closure.rs.
fn main() {
    let f = |_||x, y| x + y;
    assert_eq!(f(())(1, 2), 3);

    // `move` keeps the captured value alive in the closure it is returned from.
    let g = |a: i32| move |b: i32| a + b;
    assert_eq!(g(1)(2), 3);

    // A joined pair where the outer closure captures nothing.
    let h = |_: i32||b: i32| b * 2;
    assert_eq!(h(0)(3), 6);

    // An empty inner list still parses.
    let i = |_| || 5;
    assert_eq!(i(())(), 5);

    // A joined pair after several parameters.
    let j = |_: i32, _: i32||b: i32| b - 1;
    assert_eq!(j(1, 9)(4), 3);
}
