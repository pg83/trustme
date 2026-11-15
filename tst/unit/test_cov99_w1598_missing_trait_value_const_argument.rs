//@ compile-fail: missing_value
//@ compile-flags: --emit=metadata -Cpanic=abort

trait Lookup {}
impl Lookup for () {}

fn main() {
    let _ = <() as Lookup>::missing_value::<{ 1usize }>;
}
