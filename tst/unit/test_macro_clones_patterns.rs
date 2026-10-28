//@ compile-flags: -Zdump-ast -Zdump-hir -Zdump-mir
//@ output-contains: _1_ast.rs deref!(
//@ output-contains: _1_ast.rs box
//@ output-contains: _2_hir.rs Choice
//@ output-contains: _3_mir.rs main
#![feature(box_patterns, deref_patterns, guard_patterns)]
#![allow(incomplete_features)]

macro_rules! twice {
    ($value:expr, $pattern:pat) => {
        (match $value { $pattern => true, _ => false },
         match $value { $pattern => true, _ => false })
    };
}

struct Record { left: u8, right: u8 }
struct Tuple(u8, u8, u8);
enum Choice { Pair(u8, u8), Named { value: u8 } }
const LIMIT: u8 = 9;

fn main() {
    assert_eq!(twice!(4, -3..=8), (true, true));
    assert_eq!(twice!(8u8, 2..9), (true, true));
    assert_eq!(twice!(9u8, LIMIT), (true, true));
    assert_eq!(twice!(1.5f64, 1.5f64), (true, true));
    assert_eq!(twice!("hello", "hello"), (true, true));
    assert_eq!(twice!(b"hi", b"hi"), (true, true));
    assert_eq!(twice!(true, true), (true, true));
    assert_eq!(twice!('λ', 'λ'), (true, true));
    assert_eq!(twice!((1, 2, 3, 4), (1, .., 4)), (true, true));
    assert_eq!(twice!(Tuple(1, 2, 3), Tuple(1, .., 3)), (true, true));
    assert_eq!(twice!(Record { left: 1, right: 2 }, Record { left: 1, .. }), (true, true));
    assert_eq!(twice!(Choice::Pair(1, 2), Choice::Pair(1 | 3, 2)), (true, true));
    assert_eq!(twice!(Choice::Named { value: 3 }, Choice::Named { value: n @ 1..=4 }), (true, true));
    assert_eq!(twice!(&[1, 2, 3, 4][..], &[1, ref middle @ .., 4]), (true, true));
    assert_eq!(twice!([1, 2, 3], [1, _, 3]), (true, true));
    assert_eq!(twice!(&mut 4, &mut 4), (true, true));
    assert_eq!(twice!(Box::new(3), box 3), (true, true));
    assert_eq!(twice!(String::from("text"), deref!("text")), (true, true));
    assert_eq!(twice!(Some(2), (Some(value) if value % 2 == 0)), (true, true));
    assert_eq!(twice!(Some(3), (Some(value) if value % 2 == 0)), (false, false));
}
