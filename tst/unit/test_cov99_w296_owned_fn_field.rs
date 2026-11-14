//@ compile-fail: Couldn't find the field w296_missing_field
//@ proc-macro-aux-build: cov99_w296_dispatch_type.rs
//@ aux-build: cov99_w296_dispatch_seed.rs

fn inspect(table: cov99_w296_dispatch_type::dispatch_type!()) {
    let method = table.carry;
    let _ = method.w296_missing_field;
}

fn main() {}
