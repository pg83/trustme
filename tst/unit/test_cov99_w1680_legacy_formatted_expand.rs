//@ compile-only
//@ compile-flags: -Z stop-after=expand -Z dump-ast

fn main() {
    let _ = format_args!("{:>4}", 7u32);
}
