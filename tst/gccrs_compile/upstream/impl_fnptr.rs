extern "C" fn first() -> i32 { 1 }
extern "C" fn second() -> i32 { 2 }

pub fn compare_function_pointers() -> bool {
    let left: extern "C" fn() -> i32 = first;
    let right: extern "C" fn() -> i32 = second;
    left == right
}
