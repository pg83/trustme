//@ proc-macro-aux-build: proc_macro_item_passthrough.rs
#![feature(box_patterns, deref_patterns, guard_patterns)]
#![allow(incomplete_features)]

use proc_macro_item_passthrough::echo;

macro_rules! accepts {
    ($value:expr, $pattern:pat) => {
        echo!(match $value { $pattern => true, _ => false })
    };
}

struct Record { head: u8, tail: u8 }
struct Tuple(u8, u8, u8);

fn main() {
    assert!(accepts!(Box::new(7), box 7));
    assert!(!accepts!(Box::new(8), box 7));
    assert!(accepts!(String::from("rust"), deref!("rust")));
    assert!(!accepts!(String::from("other"), deref!("rust")));
    assert!(accepts!(Some(4), (Some(value) if value % 2 == 0)));
    assert!(!accepts!(Some(3), (Some(value) if value % 2 == 0)));
    assert!(accepts!(Record { head: 1, tail: 2 }, Record { head: 1, .. }));
    assert!(accepts!(Tuple(1, 2, 3), Tuple(1, .., 3)));
    assert!(accepts!([1, 2, 3, 4], [1, middle @ .., 4]));
    assert!(accepts!(&[1, 2, 3][..], &[1, ref rest @ ..]));
    assert!(accepts!(&mut 3, &mut 3));
    assert!(accepts!(-2i16, -4..=-1));
    assert!(!accepts!(3u8, 1..3));
    assert!(accepts!(b"hi", b"hi"));
    assert!(accepts!('λ', 'λ'));
}
