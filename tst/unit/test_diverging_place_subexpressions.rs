#![allow(dead_code, path_statements, unreachable_code)]
#![feature(never_type)]

struct Value;

fn borrow_diverging_array() {
    loop {
        let _ = &[Value, { Value; break }];
    }
    loop {
        let _ = &[Value, { Value; break }][..];
    }
}

fn project_diverging_tuple() {
    let _: &usize = &(loop {}, 1).1;
}

fn cast_never(value: !) -> u32 {
    value as u32
}

fn generic_function<T>() {}

fn cast_function_item<T>() -> u32 {
    generic_function::<T> as u32
}

fn main() {}
