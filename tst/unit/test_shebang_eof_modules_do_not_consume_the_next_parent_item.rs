//@ compile-variant: -O
//@ input: support/shebang_round265_bare.rs
//@ input: support/shebang_round265_space.rs
//@ input: support/shebang_round265_slash.rs
//@ input: support/shebang_round265_line.rs
//@ input: support/shebang_round265_line_doc.rs
//@ input: support/shebang_round265_block_doc.rs
//@ input: support/shebang_round265_block.rs
#[path = "support/shebang_round265_bare.rs"]
mod bare;
#[path = "support/shebang_round265_space.rs"]
mod space;
#[path = "support/shebang_round265_slash.rs"]
mod slash;
#[path = "support/shebang_round265_line.rs"]
mod line;
#[path = "support/shebang_round265_line_doc.rs"]
mod line_doc;
#[path = "support/shebang_round265_block_doc.rs"]
mod block_doc;
#[path = "support/shebang_round265_block.rs"]
mod block;

const AFTER_EMPTY_MODULES: usize = 37;
fn main() { assert_eq!([AFTER_EMPTY_MODULES; 3].iter().sum::<usize>(), 111); }
