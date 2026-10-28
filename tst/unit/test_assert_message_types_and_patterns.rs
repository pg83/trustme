#![feature(box_patterns, deref_patterns, guard_patterns)]
#![allow(incomplete_features, unused_variables)]

use std::panic::{catch_unwind, UnwindSafe};

fn check(f: impl FnOnce() + UnwindSafe, fragments: &[&str]) {
    let payload = catch_unwind(f).expect_err("the condition must fail");
    let text = if let Some(text) = payload.downcast_ref::<String>() {
        text.as_str()
    } else {
        payload.downcast_ref::<&str>().unwrap()
    };
    let compact: String = text.split_whitespace().collect();
    for fragment in fragments {
        let expected: String = fragment.split_whitespace().collect();
        assert!(compact.contains(&expected), "missing {fragment:?} in {text:?}");
    }
}

struct Record { left: i32, right: i32 }
struct Tuple(i32, i32, i32);

fn main() {
    check(|| assert!({
        let unit: () = ();
        let tuple: (u8,) = (1,);
        let array: [i16; 2] = [2, 3];
        let slice: &[i16] = &array;
        let text: &'static str = "hello";
        let mut value = 7u32;
        let reference: &mut u32 = &mut value;
        let pointer: *mut u32 = reference;
        let constant: *const u32 = pointer;
        let inferred: _ = false;
        inferred
    }), &["let unit: ()", "let tuple: (u8,)", "let array: [i16; 2]",
          "let slice: &[i16]", "let text: &'static str", "let reference: &mut u32",
          "let pointer: *mut u32", "let constant: *const u32", "let inferred = false"]);

    check(|| assert!({
        let callable: Option<for<'a> fn(&'a str) -> &'a str> = None;
        let foreign: Option<unsafe extern "C" fn(i32, ...) -> i32> = None;
        let never: Option<fn() -> !> = None;
        let object: &(dyn std::fmt::Debug + Send + Sync + 'static) = &1;
        let closure = |x: (u8,), y: [u8; 2]| -> bool { x.0 == y[0] };
        closure((1,), [2, 3])
    }), &["for<'a> fn(&'a str) -> &'a str", "unsafe extern \"C\" fn(i32, ...) -> i32",
          "fn() -> !", "dyn std::fmt::Debug + Send + Sync + 'static",
          "|x: (u8,), y: [u8; 2]| -> bool"]);

    check(|| assert!({
        let mut tuple = (1, 2, 3);
        let (ref first, ref mut middle, ..) = tuple;
        *middle += *first;
        let Tuple(head, .., tail) = Tuple(1, 2, 3);
        let Record { left: renamed, right } = Record { left: head, right: tail };
        let &[begin, ref rest @ .., end] = &[1, 2, 3, 4];
        let box boxed = Box::new(5);
        let string_value = String::from("hello");
        let deref!(ref string) = string_value;
        match (renamed, right, begin, end, boxed, string.as_str()) {
            (n @ -2..=2, 3, 1, 4, 5, "hello") if n > 0 => false,
            _ => true,
        }
    }), &["ref first", "ref mut middle", "Tuple(head, .., tail)",
          "Record { left: renamed, right }", "ref rest @ ..", "box boxed",
          "deref!(ref string)", "n @ -2..=2", "if n > 0"]);

    check(|| assert!(match (Some(2), b"hi" as &[u8], 'x', true, 1.5f32) {
        (Some(1 | 2), b"hi", 'x'..='z', true, 1.5f32) => false,
        _ => true,
    }), &["Some(1 | 2)", "b\"hi\"", "'x'..='z'", "true", "1.5f32"]);

    check(|| assert!(match (Some(2), 3) {
        ((Some(n) if n % 2 == 0), 3) => false,
        _ => true,
    }), &["(Some(n) if n % 2 == 0)"]);
}
