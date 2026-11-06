fn preserve<T: ?Sized>(value: &mut T) -> &mut T {
    value
}

fn main() {
    let bytes = [19u8; 37];
    let mut slice = &bytes[..];
    let mut link = &mut slice;
    let held = preserve(&mut link);
    assert_eq!(std::mem::size_of_val(&**held), std::mem::size_of::<&[u8]>());
    assert_eq!((***held).len(), 37);
    assert_eq!((***held)[11], 19);
}
