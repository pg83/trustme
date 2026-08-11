// { dg-additional-options -frust-mangling=v0 }

#![feature(lang_items)]
fn main() {
    // { dg-final { scan-assembler "_R.*NC.*NvC.*10v0_mangle24main.*0" } }
    let closure_annotated = |i: i32| -> i32 { i + 1 };
    let _ = closure_annotated(0) - 1;
}
