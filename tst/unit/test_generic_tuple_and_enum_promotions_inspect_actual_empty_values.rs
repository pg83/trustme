//@ compile-variant: -O
enum EmptyOr<T> { Empty, Full(T) }
struct Tuple<T>(Option<T>);
struct Named<T> { value: EmptyOr<T> }

fn tuple<T: 'static>() -> &'static (Option<T>, u16) { &(None, 17) }
fn wrapped<T: 'static>() -> &'static Tuple<T> { &Tuple(None) }
fn named<T: 'static>() -> &'static Named<T> { &Named { value: EmptyOr::Empty } }

fn main() {
    assert_eq!(tuple::<String>().1, 17);
    assert!(tuple::<String>().0.is_none());
    assert!(wrapped::<String>().0.is_none());
    assert!(matches!(named::<String>().value, EmptyOr::Empty));
    let full = EmptyOr::Full(23u16);
    assert!(matches!(full, EmptyOr::Full(23)));
}
