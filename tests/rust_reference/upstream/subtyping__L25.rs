// Extracted from src/subtyping.md:25
#![allow(unused)]
fn main() {
    // Here 'a is substituted for 'static
    let subtype: &(for<'a> fn(&'a i32) -> &'a i32) = &((|x| x) as fn(&_) -> &_);
    let supertype: &(fn(&'static i32) -> &'static i32) = subtype;
    
    // This works similarly for trait objects
    let subtype: &(dyn for<'a> Fn(&'a i32) -> &'a i32) = &|x| x;
    let supertype: &(dyn Fn(&'static i32) -> &'static i32) = subtype;
    
    // We can also substitute one higher-ranked lifetime for another
    let subtype: &(for<'a, 'b> fn(&'a i32, &'b i32)) = &((|x, y| {}) as fn(&_, &_));
    let supertype: &for<'c> fn(&'c i32, &'c i32) = subtype;
}
