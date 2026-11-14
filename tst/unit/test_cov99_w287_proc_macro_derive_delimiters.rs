//@ proc-macro-aux-build: cov99_w287_derive_delimiters.rs

#[derive(cov99_w287_derive_delimiters::DelimiterValue)]
#[w287_helper]
struct DelimiterProbe;

fn main() {
    assert_eq!(DelimiterProbe::value(), 287);
}
