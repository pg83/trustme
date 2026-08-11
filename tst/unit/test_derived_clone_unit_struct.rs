//@ compile-flags: -Znext-solver

#[derive(Clone)]
struct LayoutLike;

fn main() {
    let value = LayoutLike;
    let _cloned = value.clone();
}
