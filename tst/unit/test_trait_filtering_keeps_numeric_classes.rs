struct Pair<A, B>(A, B);
struct Token;
trait Route { fn route(self) -> u8; }
impl<T> Route for Pair<T, T> { fn route(self) -> u8 { 1 } }
impl Route for Pair<i32, f64> { fn route(self) -> u8 { 2 } }
impl Route for Pair<f64, i32> { fn route(self) -> u8 { 3 } }
impl Route for Pair<i32, bool> { fn route(self) -> u8 { 4 } }
impl Route for Pair<f64, bool> { fn route(self) -> u8 { 5 } }
impl Route for Pair<i32, Token> { fn route(self) -> u8 { 6 } }
impl Route for Pair<f64, Token> { fn route(self) -> u8 { 7 } }
impl Route for Pair<i32, [bool; 1]> { fn route(self) -> u8 { 8 } }
impl Route for Pair<f64, [bool; 1]> { fn route(self) -> u8 { 9 } }
fn produce<T>(make: impl FnOnce() -> T) -> T { make() }
fn route<T: Route>(value: T) -> u8 { value.route() }
fn main() {
    assert_eq!(route(produce(|| Pair(3, 1.25))), 2);
    assert_eq!(route(produce(|| Pair(1.25, 3))), 3);
    assert_eq!(route(produce(|| Pair(3, true))), 4);
    assert_eq!(route(produce(|| Pair(1.25, false))), 5);
    assert_eq!(route(produce(|| Pair(3, Token))), 6);
    assert_eq!(route(produce(|| Pair(1.25, Token))), 7);
    assert_eq!(route(produce(|| Pair(3, [false]))), 8);
    assert_eq!(route(produce(|| Pair(1.25, [true]))), 9);
    assert_eq!(route(produce(|| Pair(3, 5))), 1);
    assert_eq!(route(produce(|| Pair(1.25, 2.5))), 1);
    assert_eq!(route(produce(|| Pair(3, 5u16))), 1);
    assert_eq!(route(produce(|| Pair(1.25, 2.5f32))), 1);
}
