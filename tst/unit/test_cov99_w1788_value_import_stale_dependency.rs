//@ aux-build: cov99_w1788_before/cov99_w1788_value_api.rs
//@ aux-output-dir: cov99_w1788_before/cov99_w1788_value_api.rs=t-before
//@ aux-build: cov99_w1788_value_reexport.rs
//@ aux-build: cov99_w1788_after/cov99_w1788_value_api.rs
//@ aux-output-dir: cov99_w1788_after/cov99_w1788_value_api.rs=t-after
//@ compile-flags: --emit=metadata
//@ compile-fail: Couldn't find final component of

fn main() {
    cov99_w1788_value_reexport::vanished();
}
