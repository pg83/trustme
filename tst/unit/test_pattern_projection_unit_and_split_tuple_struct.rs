struct Marker;
struct Record<T>(u8, T, u16, u32);

fn read<T>(value: (Marker, Record<T>)) -> (u8, u32) {
    match value {
        (Marker {}, Record(first, .., last)) => (first, last),
    }
}

fn main() {
    assert_eq!(read(std::hint::black_box((Marker, Record(3, 17u8, 257, 65539)))), (3, 65539));
    assert_eq!(read((Marker, Record(7, "leaf", 11, 13))), (7, 13));
}
