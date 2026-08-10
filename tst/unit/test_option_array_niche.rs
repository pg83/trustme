use std::mem::size_of;

fn main() {
    assert_eq!(
        size_of::<[Box<isize>; 1]>(),
        size_of::<Option<[Box<isize>; 1]>>(),
    );

    let value = Some([Box::new(17)]);
    match value {
        Some([number]) => assert_eq!(*number, 17),
        None => panic!("lost array payload"),
    }

    let none: Option<[Box<isize>; 1]> = None;
    assert!(none.is_none());
}
