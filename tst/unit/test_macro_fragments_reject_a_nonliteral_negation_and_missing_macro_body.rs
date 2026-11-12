//@ compile-fail: No macro arm matched

macro_rules! reject_fragments {
    ($literal:literal) => {};
    ($block:block) => {};
    (- $pattern:pat) => {};
}

fn main() {
    reject_fragments!(- missing_body!);
}
