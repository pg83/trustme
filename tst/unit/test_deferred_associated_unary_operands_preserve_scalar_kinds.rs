trait Values { const NUMBER: i32; const FLAG: bool; const FLOAT: f64; }
struct First;
struct Second;
impl Values for First { const NUMBER: i32 = -37; const FLAG: bool = true; const FLOAT: f64 = 1.5; }
impl Values for Second { const NUMBER: i32 = 41; const FLAG: bool = false; const FLOAT: f64 = -2.5; }
fn transform<T: Values>() -> (i32, bool, f64) {
    let number = T::NUMBER;
    let flag = T::FLAG;
    let float = T::FLOAT;
    (!number, !flag, -float)
}
fn main() {
    assert_eq!(transform::<First>(), (36, false, -1.5));
    assert_eq!(transform::<Second>(), (-42, true, 2.5));
}
