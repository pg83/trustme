// A block followed by `.` or `?` starts an expression rather than standing as
// a statement of its own. That already held at block level; a match arm went
// through a different parser, which stopped at the `.`.
//
// Same shape as the gccrs test stmt_with_block_dot.rs.
fn arm_body(d: i32) -> i32 {
    match d {
        _ => { (4, 5) }.1 / 3,
    }
}

fn statement() -> i32 {
    { (0, 1) }.0 + 1
}

fn question(d: i32) -> Result<i32, ()> {
    let v = { Ok::<_, ()>(d) }?;
    Ok(v)
}

fn main() {
    assert_eq!(arm_body(0), 1);
    assert_eq!(statement(), 1);
    assert_eq!(question(7), Ok(7));
}
