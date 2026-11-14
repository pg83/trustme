//@ compile-only
//@ compile-flags: --target $DIR/inputs/cov99_w242_backend_tab_trace.toml --print cfg

fn main() {
    assert_eq!(b'\t', 9);
}
