//@ proc-macro-aux-build: item_passthrough_attribute.rs
// A binding written without a subpattern reaches a proc macro as written:
// `mut count`, `ref name`, `b` - rustc's `PatKind::Ident(.., None)` prints no
// `@`. mockall rejects "subpattern bindings" among a mocked function's
// arguments, and test-strategy rejects `mut control @ _`.
use item_passthrough_attribute::no_subpatterns;

#[no_subpatterns]
fn bump(mut count: u32, ref name: String, (a, mut b): (u8, u8)) -> u32 {
    count += 1;
    b += a;
    count + name.len() as u32 + b as u32
}

fn main() {
    assert_eq!(bump(1, String::from("ab"), (1, 2)), 2 + 2 + 3);
}
