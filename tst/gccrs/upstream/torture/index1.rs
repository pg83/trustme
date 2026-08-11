use std::ops::Index;

struct Foo(i32, i32);

impl Index<isize> for Foo {
    type Output = i32;

    fn index(&self, index: isize) -> &i32 {
        if index == 0 { &self.0 } else { &self.1 }
    }
}

fn gccrs_main() -> i32 {
    let value = Foo(1, 2);
    value[1] - value[0] - 1
}

fn main() { let code = gccrs_main(); if code != 0 { std::process::exit(code); } }
