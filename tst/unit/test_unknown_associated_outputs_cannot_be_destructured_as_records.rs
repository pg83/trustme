//@ compile-fail: struct
#![feature(more_qualified_paths)]

trait Project { type Output; }

fn extract<T: Project>(record: <T as Project>::Output) {
    let <T as Project>::Output { value } = record;
    let _ = value;
}

fn main() {}
