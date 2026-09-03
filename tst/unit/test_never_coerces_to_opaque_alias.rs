// A diverging tail coerces to whatever the function returns, and the plan for
// that coercion is always the same: the value never arrives, so nothing is
// adjusted. When the destination is an opaque alias already related to its
// hidden type, the coercion is settled before the diverging source is ever
// looked at, and the answer comes back a proven coercion with no plan to carry
// out. The compiler aborted on it.

#![feature(type_alias_impl_trait)]

pub type Alias = impl Copy;

#[define_opaque(Alias)]
fn bop(stop: bool) -> Alias {
    let value: Alias = 1u8;
    if stop {
        return value;
    }
    loop {}
}

fn main() {
    let _kept = bop(true);
}
