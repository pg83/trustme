//@ compile-fail: Type mismatch between
//@ compile-flags: -Zdump-hir

trait Fields {
    type First;
    type Second;
}

fn convert(source: &dyn Fields<First = u16>) {
    let _: &dyn Fields<Second = u16> = source;
}

fn main() {}
