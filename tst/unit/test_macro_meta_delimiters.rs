macro_rules! accept_meta {
    ($meta:meta) => {};
}

accept_meta!(meta(words inside));
accept_meta!(meta[words inside]);
accept_meta!(meta { words inside });

fn main() {}
