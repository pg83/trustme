const fn array_edges(values: [u16; 4]) -> [u16; 2] {
    let [first, _, _, last] = values;
    [first, last]
}

const fn slice_edges(values: &[u16]) -> [u16; 2] {
    match values {
        [first, .., last] => [*first, *last],
        _ => [0, 0],
    }
}

const DATA: [u16; 4] = [317, 331, 337, 347];
const ARRAY_EDGES: [u16; 2] = array_edges(DATA);
const SLICE_EDGES: [u16; 2] = slice_edges(&DATA);
const FLOATS: (f32, f64) = (1.25f32, 2.5f64);

trait Select<T> {
    fn pick(value: T) -> T { value }
}

struct Holder<T>(T);
impl<T> Select<T> for Holder<T> {}

fn main() {
    assert_eq!(ARRAY_EDGES, [317, 347]);
    assert_eq!(SLICE_EDGES, ARRAY_EDGES);
    assert_eq!(slice_edges(&[353]), [0, 0]);
    assert_eq!(FLOATS.0 + 1.25f32, FLOATS.1 as f32);
    let constructor: fn(u16) -> Holder<u16> = Holder::<u16>;
    let holder = constructor(359);
    assert_eq!(<Holder<u16> as Select<u16>>::pick(holder.0), 359);
}
