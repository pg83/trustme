// arrayvec's tests: `let t = || -> Result<(), Box<dyn Error>> { .. }();`.
// Upstream parses the body of a closure with an explicit return type as a
// block and nothing more (`parse_expr_closure` takes `parse_expr_block`), so
// the closure is a complete operand and the `()` after it calls it. We parsed
// the body as a whole expression and called the block's value instead.
use std::error::Error;

fn main() {
    let t = || -> Result<u8, Box<dyn Error>> {
        let v: u8 = "7".parse()?;
        Ok(v)
    }();
    assert_eq!(t.unwrap(), 7);
    let n = |x: u8| -> u8 { x + 1 }(4);
    assert_eq!(n, 5);
    let f = |x: u8| x + 1;
    assert_eq!(f(1), 2);
}
