// Extracted from library/core/src/intrinsics/mir.rs:264
#![allow(unused)]
#![allow(internal_features)]
#![feature(core_intrinsics, custom_mir)]
fn main() {
    
    use core::intrinsics::mir::*;
    
    #[custom_mir(dialect = "built")]
    fn debuginfo(arg: Option<&i32>) {
        mir!(
            // Debuginfo for a source variable `plain_local` that just duplicates `arg`.
            debug plain_local => arg;
            // Debuginfo for a source variable `projection` that can be computed by dereferencing
            // a field of `arg`.
            debug projection => *Field::<&i32>(Variant(arg, 1), 0);
            // Debuginfo for a source variable `constant` that always holds the value `5`.
            debug constant => 5_usize;
            {
                Return()
            }
        )
    }
}
