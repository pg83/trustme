// derive_more 0.99's generated parser: `#![inline]` as the first line of a
// function body is an inner attribute of the function itself.
fn slice_eq(input: &str, pos: usize, m: &'static str) -> bool {
    #![inline]
    #![allow(dead_code)]
    let l = m.len();
    input.len() >= pos + l && &input.as_bytes()[pos..pos + l] == m.as_bytes()
}

fn main() {
    assert!(slice_eq("display", 0, "dis"));
    assert!(!slice_eq("display", 1, "dis"));
}
