//@ proc-macro-aux-build: cov99_w525_record_transport.rs
//@ aux-build: cov99_w525_collision_record.rs
//@ env: TRUSTME_DUMP_PROCMACRO=$OUT-w525
//@ compile-fail: Named value referring to a struct that isn't tuple-like or unit-like

fn main() {
    let _ = cov99_w525_record_transport::value!();
}
