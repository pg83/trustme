// { dg-options "-w" }

union B {
    a: A,
    b: f32,
}

#[derive(Clone, Copy)]
struct A {
    data: i32,
    len: usize,
}
