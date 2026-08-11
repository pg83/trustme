#![feature(never_type)]
pub fn clone_never(value: &!) -> ! { value.clone() }
