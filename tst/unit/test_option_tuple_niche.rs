use std::mem::size_of;

fn main() {
    assert_eq!(
        size_of::<(u8, Box<isize>)>(),
        size_of::<Option<(u8, Box<isize>)>>(),
    );

    let value = Some((3, Box::new(17)));
    match value {
        Some((tag, number)) => assert_eq!((tag, *number), (3, 17)),
        None => panic!("lost tuple payload"),
    }

    let none: Option<(u8, Box<isize>)> = None;
    assert!(none.is_none());
}
