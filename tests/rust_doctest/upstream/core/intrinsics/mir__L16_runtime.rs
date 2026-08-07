// Extracted from library/core/src/intrinsics/mir.rs:16
#![allow(unused)]
#![feature(core_intrinsics, custom_mir)]
#![allow(internal_features)]
fn main() {

    use core::intrinsics::mir::*;

    #[custom_mir(dialect = "built")]
    pub fn simple(x: i32) -> i32 {
        mir! {
            let temp2: i32;

            {
                let temp1 = x;
                Goto(my_second_block)
            }

            my_second_block = {
                temp2 = Move(temp1);
                RET = temp2;
                Return()
            }
        }
    }
}
