//@ compile-fail: Tuple literal used where a non-tuple expected

fn main() {
    let &_ = (0u8,);
}
