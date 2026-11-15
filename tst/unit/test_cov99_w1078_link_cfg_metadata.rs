//@ compile-only
//@ compile-flags: --emit=metadata

#![feature(link_cfg)]

#[link(name = "cov99_w1078_metadata_only", cfg(all()))]
extern "C" {}

fn main() {}
