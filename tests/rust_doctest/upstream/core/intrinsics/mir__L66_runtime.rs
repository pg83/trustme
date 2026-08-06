// Extracted from library/core/src/intrinsics/mir.rs:66
#![allow(unused)]
#![feature(core_intrinsics, custom_mir)]
#![allow(internal_features)]
#![allow(unused_assignments)]
fn main() {
    
    use core::intrinsics::mir::*;
    
    #[custom_mir(dialect = "built")]
    pub fn choose_load(a: &i32, b: &i32, c: bool) -> i32 {
        mir! {
            {
                match c {
                    true => t,
                    _ => f,
                }
            }
    
            t = {
                let temp = a;
                Goto(load_and_exit)
            }
    
            f = {
                temp = b;
                Goto(load_and_exit)
            }
    
            load_and_exit = {
                RET = *temp;
                Return()
            }
        }
    }
    
    #[custom_mir(dialect = "built")]
    fn unwrap_unchecked<T>(opt: Option<T>) -> T {
        mir! {
            {
                RET = Move(Field(Variant(opt, 1), 0));
                Return()
            }
        }
    }
    
    #[custom_mir(dialect = "runtime", phase = "optimized")]
    fn push_and_pop<T>(v: &mut Vec<T>, value: T) {
        mir! {
            let _unused;
            let popped;
    
            {
                Call(_unused = Vec::push(v, value), ReturnTo(pop), UnwindContinue())
            }
    
            pop = {
                Call(popped = Vec::pop(v), ReturnTo(drop), UnwindContinue())
            }
    
            drop = {
                Drop(popped, ReturnTo(ret), UnwindContinue())
            }
    
            ret = {
                Return()
            }
        }
    }
    
    #[custom_mir(dialect = "runtime", phase = "optimized")]
    fn annotated_return_type() -> (i32, bool) {
        mir! {
            type RET = (i32, bool);
            {
                RET.0 = 1;
                RET.1 = true;
                Return()
            }
        }
    }
}
