// A line comment ends at the newline, so a bare carriage return inside one is
// content rather than a terminator. The lexer stopped at the CR and tried to
// parse the rest of the comment as code.
//
// A doc comment is different: rustc rejects a bare CR there, so this only
// covers the plain form.
//
// Same shape as the gccrs test torture/isolated_cr_line_comment.rs.
// comment cr is allowed
fn main() {
    let x = 1;
    assert_eq!(x, 1); // trailing cr too
}
