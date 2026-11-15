//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort
//@ compile-fail: `missing_value` is not a value member of trait

trait ValueNamespace {}

fn probe<T: ValueNamespace>() {
    let _ = <T as ValueNamespace>::missing_value;
}
