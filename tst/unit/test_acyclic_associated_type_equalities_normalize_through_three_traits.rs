trait Base { type Value; }
trait Middle { type Value; }
trait Top { type Value; }

struct Chain;
impl Base for Chain { type Value = u32; }
impl Middle for Chain { type Value = u32; }
impl Top for Chain { type Value = u32; }

fn forward<T>(value: <T as Top>::Value) -> <T as Base>::Value
where
    T: Top<Value = <T as Middle>::Value>
        + Middle<Value = <T as Base>::Value>
        + Base,
{
    let middle: <T as Middle>::Value = value;
    let base: <T as Base>::Value = middle;
    base
}

fn main() {
    assert_eq!(forward::<Chain>(73), 73);
}
