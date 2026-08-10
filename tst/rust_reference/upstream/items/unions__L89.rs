// Extracted from src/items/unions.md:89
#![allow(unused)]
fn main() {
    union MyUnion { f1: u32, f2: f32 }
    let u = MyUnion { f1: 1 };
    
    unsafe {
        let f = u.f1;
    }
}
