//@ compile-flags: -Zdump-mir

struct Leaf(u32);
struct Inner(Leaf);
struct Outer(Inner);

#[inline(never)]
fn projected(input: Outer) -> u32 {
    let source = input;
    std::hint::black_box(&source);
    let alias = source;
    let inner = alias.0;
    let leaf = inner.0;
    leaf.0
}

fn main() {
    assert_eq!(projected(Outer(Inner(Leaf(29)))), 29);
    assert_eq!(projected(Outer(Inner(Leaf(0xffff_1234)))), 0xffff_1234);
}
