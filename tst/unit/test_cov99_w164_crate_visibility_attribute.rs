//@ proc-macro-aux-build: cov99_w164_erase_crate_item.rs

#[cov99_w164_erase_crate_item::erase]
crate struct LegacyCrateItem;

fn main() {}
