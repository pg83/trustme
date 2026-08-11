// { dg-additional-options "-fdump-tree-gimple" }
#![feature(rustc_attrs)]

#[rustc_std_internal_symbol]
pub fn _foo() -> i32 {
    0
}

// { dg-final { scan-tree-dump "_foo" "gimple" } }
