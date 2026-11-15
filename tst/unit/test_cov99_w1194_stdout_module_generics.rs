//@ proc-macro-aux-build: cov99_w1194_stdout_module_generics.rs
//@ compile-fail: Type parameters were not expected here
//@ env: TRUSTME_DUMP_PROCMACRO=$OUT-w1194

fn main() {
    const VALUE: u8 = 1;
    let _ = cov99_w1194_stdout_module_generics::module_args!();
}
