// `assert!('d: { break 'd true })`: the builtin `assert!` hands its
// condition on as an expression fragment, which is copied when the
// expansion is parsed. Upstream's AST is copied whole (`#[derive(Clone)]`
// on `Block`/`ExprKind::Block(_, label)`); a copy that dropped the block's
// label left `break 'd` with nothing to break out of.
fn main() {
    assert!('d: { break 'd true });
    assert!('e: {
        if 1 + 1 == 2 {
            break 'e true;
        }
        false
    });
    let x = 'b: { break 'b 3 };
    assert_eq!(x, 3);
}
