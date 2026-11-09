//@ compile-fail: forbidden by an enclosing item
#![forbid(unused_mut, reason = "keep the parent restriction")]

#[allow(unused_mut, reason = "a child cannot lower forbid")]
fn main() {}
