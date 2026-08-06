// Extracted from library/core/src/intrinsics/mir.rs:445
#![allow(unused)]
#![allow(internal_features)]
#![feature(custom_mir, core_intrinsics)]
fn main() {
    
    use core::intrinsics::mir::*;
    
    #[custom_mir(dialect = "built")]
    fn unwrap_deref(opt: Option<&i32>) -> i32 {
        mir! {
            {
                RET = *Field::<&i32>(Variant(opt, 1), 0);
                Return()
            }
        }
    }
    
    #[custom_mir(dialect = "built")]
    fn set(opt: &mut Option<i32>) {
        mir! {
            {
                place!(Field(Variant(*opt, 1), 0)) = 5;
                Return()
            }
        }
    }
}
