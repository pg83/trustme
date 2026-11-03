//@ compile-variant: -O
trait Project { type Output; }
struct Identity<T>(T);
impl<T> Project for Identity<T> { type Output = (T, T); }
fn duplicate<T: Copy>(value: T) -> <Identity<T> as Project>::Output { (value, value) }
fn through<T: Project>(value: T::Output) -> T::Output { value }
fn main() {
    assert_eq!(through::<Identity<u16>>(duplicate(839u16)), (839, 839));
    assert_eq!(through::<Identity<char>>(duplicate('Я')), ('Я', 'Я'));
}
