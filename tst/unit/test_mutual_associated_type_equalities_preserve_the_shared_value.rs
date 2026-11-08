//@ compile-fail: cycle detected while normalizing associated types
trait Left {
    type Value;
}
trait Right {
    type Value;
}

struct Agreement;
impl Left for Agreement { type Value = u16; }
impl Right for Agreement { type Value = u16; }

fn transfer<T>(value: <T as Left>::Value) -> <T as Right>::Value
where
    T: Left<Value = <T as Right>::Value> + Right<Value = <T as Left>::Value>,
{
    let same: <T as Left>::Value = value;
    same
}

fn main() {
    assert_eq!(transfer::<Agreement>(47), 47);
}
