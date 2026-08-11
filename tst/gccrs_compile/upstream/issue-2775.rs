// { dg-options "-w" }

#![feature(lang_items)]
struct Ref<'a, T> {
    x: &'a T,
}

pub fn test<'a, 'b, 'c: 'b>() {
    let (_, &&Ref::<(&'_ i32, i32)> { x: &(a, b) }): (i32, &'_ &'b Ref<'b, (&'c i32, i32)>);
}
