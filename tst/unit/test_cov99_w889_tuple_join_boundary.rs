//@ crate-type: lib
//@ compile-flags: -Z mir-opt-level=2 -Z dump-mir

pub fn selected_field(flag: bool, left: ((u32,),), right: ((u32,),)) -> u64 {
    let selected = if flag { left } else { right };
    selected.0.0 as u64
}
