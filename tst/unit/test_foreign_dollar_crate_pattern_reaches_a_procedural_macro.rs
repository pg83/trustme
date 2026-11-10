//@ aux-build: own_foreign_pattern_source.rs
//@ proc-macro-aux-build: own_pattern_token_receiver.rs

macro_rules! capture_pattern {
    ($pattern:pat) => { own_pattern_token_receiver::receive!($pattern) };
}

fn main() {
    let own_foreign_pattern_source::Mark = own_foreign_pattern_source::Mark;
    assert_eq!(own_foreign_pattern_source::relay_pattern!(capture_pattern), 43);
}
