// Extracted from src/types/closure.md:166
#![allow(unused)]
fn main() {
    struct Int(i32);
    struct B<'a>(&'a i32);
    
    struct MyStruct<'a> {
       a: &'static Int,
       b: B<'a>,
    }
    
    fn foo<'a, 'b>(m: &'a MyStruct<'b>) -> impl FnMut() + 'static {
        let c = || drop(&m.a.0);
        c
    }
}
