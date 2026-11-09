enum Packet<T> {
    Data(T),
}

struct Envelope<T> {
    value: T,
}

fn pass<T>(value: T) -> T {
    value
}

fn calculate<F: Fn(u32) -> u32>(callback: F) -> u32 {
    let envelope = Envelope {
        value: Packet::Data(pass([3u16, 5])),
    };
    let Packet::Data(values) = envelope.value;
    let slice: &[u16] = &values;
    callback(slice[0] as u32) + slice.len() as u32
}

fn main() {
    assert_eq!(calculate(|value| value + 1), 6);
}
