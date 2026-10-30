#![feature(core_intrinsics,const_eval_select)]
#![allow(internal_features)]
const fn constant<const N:usize>(value:u16) -> [u16;N] { [value;N] }
#[inline(never)]
fn runtime<const N:usize>(value:u16) -> [u16;N] { [value;N] }
pub const fn selected<const N:usize>(value:u16) -> [u16;N] { std::intrinsics::const_eval_select((value,),constant::<N>,runtime::<N>) }
