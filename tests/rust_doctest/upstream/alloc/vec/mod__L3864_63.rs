// Extracted from library/alloc/src/vec/mod.rs:3864
#![allow(unused)]
extern crate alloc;
fn main() {
    let some_predicate = |x: &mut i32| { *x % 2 == 1 };
    let mut vec = vec![0, 1, 2, 3, 4, 5, 6];
    let mut vec2 = vec.clone();
    let range = 1..5;
    let mut i = range.start;
    let end_items = vec.len() - range.end;
    let mut extracted = vec![];

    while i < vec.len() - end_items {
        if some_predicate(&mut vec[i]) {
            let val = vec.remove(i);
            extracted.push(val);
            // your code here
        } else {
            i += 1;
        }
    }

    let extracted2: Vec<_> = vec2.extract_if(range, some_predicate).collect();
    assert_eq!(vec, vec2);
    assert_eq!(extracted, extracted2);
}
