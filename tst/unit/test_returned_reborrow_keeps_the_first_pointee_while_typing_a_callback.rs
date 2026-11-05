fn inspect<'a, T: ?Sized, F>(value: &'a T, callback: F) -> (&'a T, usize)
where
    F: FnOnce(&T) -> usize,
{
    (value, callback(value))
}

fn main() {
    let boxed = Box::new(37u16);
    let mut chain = &&boxed;
    let (view, bytes) = inspect(&mut chain, |inner| std::mem::size_of_val(inner));
    assert_eq!(
        std::any::type_name_of_val(view),
        std::any::type_name::<&&Box<u16>>()
    );
    assert_eq!(bytes, std::mem::size_of::<&&Box<u16>>());
}
