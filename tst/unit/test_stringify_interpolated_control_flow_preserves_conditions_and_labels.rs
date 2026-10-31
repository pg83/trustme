//@ edition: 2024
macro_rules! spelling { ($value:expr) => { stringify!($value) }; }
fn compact(text: &str) -> String { text.chars().filter(|c| !c.is_whitespace()).collect() }
fn main() {
    for (actual, expected) in [
        (spelling!(if let Some(x) = value && x > 3 { x } else { 0 }), "if let Some(x) = value && x > 3 { x } else { 0 }"),
        (spelling!(while let Some(x) = iter.next() && x > 0 { break; }), "while let Some(x) = iter.next() && x > 0 { break; }"),
        (spelling!({ let Some(x) = (if flag { Some(1) } else { None }) else { return }; x }), "{ let Some(x) = (if flag { Some(1) } else { None }) else { return }; x }"),
        (spelling!(break 'done ('inner: loop { break 'inner 17 })), "break 'done ('inner: loop { break 'inner 17 })"),
        (spelling!('outer: loop { break 'outer 37 }), "'outer: loop { break 'outer 37 }"),
        (spelling!(return if flag { 3 } else { 5 }), "return if flag { 3 } else { 5 }"),
        (spelling!(unsafe { *pointer = 13; pointer.read() }), "unsafe { *pointer = 13; pointer.read() }"),
    ] { assert_eq!(compact(actual), compact(expected)); }
}
