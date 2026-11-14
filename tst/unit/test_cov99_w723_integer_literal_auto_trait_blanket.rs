//@ compile-only
//@ compile-flags: --emit=metadata

trait AcceptLiteral {}

impl<T: Send> AcceptLiteral for T {}

fn accept<T: AcceptLiteral>(_: T) {}

fn main() {
    accept(0);
}
