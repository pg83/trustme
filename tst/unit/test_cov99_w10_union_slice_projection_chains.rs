#[derive(Clone, Copy)]
struct Marker;

#[derive(Clone, Copy)]
struct Pair(u16, u16);

union Value {
    marker: Marker,
    pair: Pair,
}

fn read_pair(value: &Value) -> u16 {
    unsafe { value.pair.0 + value.pair.1 }
}

fn ends(values: &[Pair], index: usize) -> u16 {
    match values {
        [first, middle @ .., last] => {
            first.0 + last.1 + middle[index].0
        }
        _ => 0,
    }
}

fn main() {
    let value = Value { pair: Pair(17, 25) };
    assert_eq!(read_pair(&value), 42);
    let marker = Value { marker: Marker };
    let Marker {} = unsafe { marker.marker };
    let values = [Pair(1, 2), Pair(3, 4), Pair(5, 6)];
    assert_eq!(ends(&values, 0), 10);
    let [first, _, last] = values;
    assert_eq!(first.1 + last.0, 7);
    let boxed = Box::new(value);
    assert_eq!(read_pair(&boxed), 42);
    let pointer = &*boxed as *const Value;
    assert_eq!(unsafe { (*pointer).pair.0 }, 17);
}
