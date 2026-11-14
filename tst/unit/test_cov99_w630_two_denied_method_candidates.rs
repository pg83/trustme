//@ aux-build: cov99_w630_stability_choices.rs

use cov99_w630_stability_choices::{FirstDenied, SecondDenied};

trait Available {
    fn cov99_w630_pick(&self) -> u8 { 7 }
}

impl Available for u8 {}

fn main() {
    assert_eq!(3u8.cov99_w630_pick(), 7);
}
