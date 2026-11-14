//@ aux-build: cov99_w525_named_record.rs
//@ compile-fail: Couldn't find variable name 'Record'

fn main() {
    let _ = cov99_w525_named_record::Record;
}
